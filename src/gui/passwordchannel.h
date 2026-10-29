// The window shows the password box (GRND-0102). sudo -A starts askpass
// mode as a process of its own, whose box the desktop does not tie to the
// window, so a click on the window could hide it. So the run page listens
// on a socket in a folder only the user can open, askpass mode asks
// through it, and the box opens as the window's own (docs/design.md, Root).
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QSocketNotifier;
class QWidget;

namespace gw {

// Names the socket for askpass mode; the run page sets it for the Worker,
// whose sudo passes it on.
inline constexpr char kAskpassSocketVar[] = "GROUNDWORK_ASKPASS_SOCKET";

class PasswordChannel : public QObject
{
    Q_OBJECT
public:
    // owner is the box's parent, so the box stays in front of its window.
    explicit PasswordChannel(QWidget *owner);
    ~PasswordChannel() override;

    // True once listening; false leaves askpass mode to show its own box.
    bool listen();
    QString path() const { return m_path; }
    void close();

private:
    void answer();

    QWidget *m_owner;
    int m_fd = -1;
    QString m_dir;
    QString m_path;
    QSocketNotifier *m_notifier = nullptr;
};

enum class Asked { NoChannel, Answered, Cancelled };

// Askpass mode's side: asks the window listening at path for the password
// sudoPrompt names. On Answered, password holds it and a newline.
Asked askWindow(const QString &path, const QString &sudoPrompt, QByteArray *password);

} // namespace gw
