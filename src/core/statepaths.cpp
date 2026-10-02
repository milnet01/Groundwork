#include "statepaths.h"

#include <QDir>

namespace gw {

QString defaultStateDir()
{
    QString base = qEnvironmentVariable("XDG_STATE_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/state");
    return base + QStringLiteral("/groundwork");
}

QString stopFilePath(const QString &stateDir) { return stateDir + QStringLiteral("/stop.request"); }

} // namespace gw
