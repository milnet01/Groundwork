#include "privilege.h"

#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>

namespace gw {

Privilege::Privilege()
{
    m_timer.setInterval(KeepAliveMs);
    connect(&m_timer, &QTimer::timeout, this, [] {
        // Started by this process, so it refreshes this process's record.
        QProcess::execute(QStringLiteral("sudo"), {QStringLiteral("-n"), QStringLiteral("-v")});
    });
}

bool Privilege::authenticate()
{
    QProcess sudo;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QStringList args;
    if (env.contains(QStringLiteral("DISPLAY")) || env.contains(QStringLiteral("WAYLAND_DISPLAY"))) {
        // SUDO_ASKPASS names a program path and carries no option, so the
        // askpass mode is chosen by an environment variable (design, Entry).
        env.insert(QStringLiteral("SUDO_ASKPASS"), QCoreApplication::applicationFilePath());
        env.insert(QStringLiteral("GROUNDWORK_ASKPASS"), QStringLiteral("1"));
        args << QStringLiteral("-A");
    }
    args << QStringLiteral("-v");
    sudo.setProcessEnvironment(env);
    sudo.setProcessChannelMode(QProcess::ForwardedChannels);
    sudo.setInputChannelMode(QProcess::ForwardedInputChannel);
    sudo.start(QStringLiteral("sudo"), args);
    if (!sudo.waitForStarted())
        return false;
    sudo.waitForFinished(-1);
    return sudo.exitStatus() == QProcess::NormalExit && sudo.exitCode() == 0;
}

void Privilege::startKeepAlive() { m_timer.start(); }

void Privilege::stopKeepAlive() { m_timer.stop(); }

QStringList Privilege::asRoot(const QStringList &argv)
{
    return QStringList{QStringLiteral("sudo"), QStringLiteral("-n"), QStringLiteral("--")} + argv;
}

} // namespace gw
