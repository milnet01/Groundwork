#include "passwordchannel.h"

#include "askpassdialog.h"

#include <QDir>
#include <QFile>
#include <QSocketNotifier>
#include <QWidget>

#include <cerrno>
#include <cstring>
#include <memory>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace gw {

namespace {

// The prompt is a user name or one line of PAM's; more is not a prompt.
constexpr qsizetype kMaxPrompt = 4096;

bool fillAddress(const QString &path, sockaddr_un *address)
{
    const QByteArray bytes = QFile::encodeName(path);
    if (bytes.size() >= qsizetype(sizeof address->sun_path))
        return false;
    std::memset(address, 0, sizeof *address);
    address->sun_family = AF_UNIX;
    std::memcpy(address->sun_path, bytes.constData(), size_t(bytes.size()));
    return true;
}

// Reads until the other end shuts its side, or limit bytes.
QByteArray readAll(int fd, qsizetype limit)
{
    QByteArray data;
    char buffer[512];
    while (data.size() < limit) {
        const ssize_t got = ::read(fd, buffer, sizeof buffer);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            break;
        data.append(buffer, got);
    }
    return data;
}

void writeAll(int fd, const QByteArray &data)
{
    qsizetype done = 0;
    while (done < data.size()) {
        const ssize_t put = ::send(fd, data.constData() + done, size_t(data.size() - done), MSG_NOSIGNAL);
        if (put < 0 && errno == EINTR)
            continue;
        if (put <= 0)
            return;
        done += put;
    }
}

} // namespace

PasswordChannel::PasswordChannel(QWidget *owner) : QObject(owner), m_owner(owner) {}

PasswordChannel::~PasswordChannel() { close(); }

bool PasswordChannel::listen()
{
    if (m_fd >= 0)
        return true;
    // mkdtemp makes the folder the user's alone (mode 0700), so no other
    // account can reach the socket inside it.
    QString base = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (base.isEmpty())
        base = QDir::tempPath();
    QByteArray dir = QFile::encodeName(base + QStringLiteral("/groundwork-XXXXXX"));
    if (!::mkdtemp(dir.data()))
        return false;
    m_dir = QFile::decodeName(dir);
    m_path = m_dir + QStringLiteral("/askpass");
    sockaddr_un address;
    m_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (m_fd < 0 || !fillAddress(m_path, &address)
        || ::bind(m_fd, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0 || ::listen(m_fd, 4) != 0) {
        close();
        return false;
    }
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &PasswordChannel::answer);
    return true;
}

void PasswordChannel::close()
{
    delete m_notifier;
    m_notifier = nullptr;
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
    if (!m_path.isEmpty())
        QFile::remove(m_path);
    if (!m_dir.isEmpty())
        QDir().rmdir(m_dir);
    m_path.clear();
    m_dir.clear();
}

void PasswordChannel::answer()
{
    const int client = ::accept4(m_fd, nullptr, nullptr, SOCK_CLOEXEC);
    if (client < 0)
        return;
    // Askpass mode sends its prompt as soon as it connects; one that sends
    // nothing must not hold the window.
    timeval wait{2, 0};
    ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof wait);
    const QString prompt = QString::fromLocal8Bit(readAll(client, kMaxPrompt));
    auto *box = new AskpassDialog(prompt, m_owner);
    box->setAttribute(Qt::WA_DeleteOnClose);
    // Hanging up with nothing written reads as Cancel, also when the
    // window closes before the box is answered.
    auto open = std::make_shared<int>(client);
    auto hangUp = [open] {
        if (*open >= 0)
            ::close(*open);
        *open = -1;
    };
    connect(box, &QDialog::finished, box, [box, open, hangUp](int result) {
        if (result == QDialog::Accepted)
            writeAll(*open, box->password().toUtf8() + '\n');
        hangUp();
    });
    connect(box, &QObject::destroyed, hangUp);
    box->open();
}

Asked askWindow(const QString &path, const QString &sudoPrompt, QByteArray *password)
{
    sockaddr_un address;
    if (!fillAddress(path, &address))
        return Asked::NoChannel;
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return Asked::NoChannel;
    if (::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0) {
        ::close(fd);
        return Asked::NoChannel;
    }
    writeAll(fd, sudoPrompt.toLocal8Bit());
    ::shutdown(fd, SHUT_WR);
    // No limit on the wait: the user may take their time to type.
    *password = readAll(fd, kMaxPrompt);
    ::close(fd);
    return password->isEmpty() ? Asked::Cancelled : Asked::Answered;
}

} // namespace gw
