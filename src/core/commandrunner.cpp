#include "commandrunner.h"

#include <QProcess>

namespace gw {

CommandResult CommandRunner::run(const QStringList &argv, int timeoutMs) const
{
    CommandResult result;
    if (argv.isEmpty())
        return result;

    QProcess process;
    // Checks parse English output, so the locale is fixed.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    process.setProcessEnvironment(env);
    process.start(argv.first(), argv.mid(1), QIODevice::ReadOnly);
    if (!process.waitForStarted())
        return result;
    result.started = true;

    if (!process.waitForFinished(timeoutMs)) {
        result.timedOut = true;
        process.kill();
        process.waitForFinished();
    } else if (process.exitStatus() == QProcess::NormalExit) {
        result.exitCode = process.exitCode();
    }
    result.out = process.readAllStandardOutput();
    result.err = process.readAllStandardError();
    return result;
}

} // namespace gw
