// Root for the Worker (docs/design.md, Root). Every sudo is started by the
// Worker process itself, never by a helper or a shell: with no terminal,
// sudo keys its remembered password to the process that started it (man
// sudoers, timestamp_type, default tty, which falls back to the parent
// process). This file is the only one that runs sudo.
#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>

namespace gw {

class Privilege : public QObject
{
    Q_OBJECT
public:
    static constexpr int KeepAliveMs = 60000;

    Privilege();

    // Asks for the password once. With a display, sudo -A calls this
    // program in askpass mode; without one, sudo asks on the terminal.
    bool authenticate();

    // Re-runs `sudo -n -v` on a timer while the event loop runs.
    void startKeepAlive();
    void stopKeepAlive();

    // The argument list that runs argv as root without prompting again.
    static QStringList asRoot(const QStringList &argv);

private:
    QTimer m_timer;
};

} // namespace gw
