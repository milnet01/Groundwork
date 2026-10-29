#include "statepaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>

namespace gw {

QString defaultStateDir()
{
    QString base = qEnvironmentVariable("XDG_STATE_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/state");
    return base + QStringLiteral("/groundwork");
}

QString stopFilePath(const QString &stateDir) { return stateDir + QStringLiteral("/stop.request"); }

bool makeStateDir(const QString &dir)
{
    // Others lose their access; the owner's is left as it is.
    constexpr QFile::Permissions owner = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner;
    return QDir().mkpath(dir) && QFile::setPermissions(dir, QFileInfo(dir).permissions() & owner);
}

QString wizardLockPath(const QString &stateDir) { return stateDir + QStringLiteral("/wizard.lock"); }

bool anotherCopyHolds(QLockFile &lock)
{
    return !lock.tryLock(0) && lock.error() == QLockFile::LockFailedError;
}

} // namespace gw
