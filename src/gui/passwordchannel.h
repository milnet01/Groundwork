// The run page asks for the password (GRND-0102, GRND-0108). sudo -A starts
// askpass mode as a process of its own, whose box the desktop does not tie
// to the window, so a click on the window could hide it. So the run page
// listens on a socket in a folder only the user can open, askpass mode asks
// through it, and the page shows the field (docs/design.md, Root).
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QSocketNotifier;

namespace gw {

// Names the socket for askpass mode; the run page sets it for the Worker,
// whose sudo passes it on.
inline constexpr char kAskpassSocketVar[] = "GROUNDWORK_ASKPASS_SOCKET";

class PasswordChannel : public QObject
{
    Q_OBJECT
public:
    explicit PasswordChannel(QObject *parent = nullptr);
    ~PasswordChannel() override;

    // True once listening; false leaves askpass mode to show its own box.
    bool listen();
    QString path() const { return m_path; }
    // Also hangs up on a request not yet answered.
    void close();

    // Answer the request asked() named; refuse() reads as Cancel.
    void reply(const QString &password);
    void refuse();

signals:
    // sudoPrompt is whose password sudo wants. A second request before an
    // answer replaces the first, which is refused.
    void asked(const QString &sudoPrompt);

private:
    void take();

    int m_fd = -1;
    int m_client = -1;
    QString m_dir;
    QString m_path;
    QSocketNotifier *m_notifier = nullptr;
};

enum class Asked { NoChannel, Answered, Cancelled };

// Askpass mode's side: asks the window listening at path for the password
// sudoPrompt names. On Answered, password holds it and a newline.
Asked askWindow(const QString &path, const QString &sudoPrompt, QByteArray *password);

} // namespace gw
