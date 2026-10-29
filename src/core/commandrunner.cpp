#include "commandrunner.h"

#include <QDeadlineTimer>
#include <QProcess>

namespace gw {

CommandResult CommandRunner::run(const QStringList &argv, int timeoutMs, qint64 maxOutBytes) const
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

    if (maxOutBytes < 0) {
        if (!process.waitForFinished(timeoutMs))
            result.timedOut = true;
    } else {
        // Read as it comes, so a program printing without end is stopped at
        // the cap instead of filling memory.
        const QDeadlineTimer deadline(timeoutMs);
        while (process.state() != QProcess::NotRunning && !deadline.hasExpired()) {
            if (!process.waitForReadyRead(int(deadline.remainingTime())) && process.state() != QProcess::NotRunning)
                process.waitForFinished(int(deadline.remainingTime()));
            result.out += process.readAllStandardOutput();
            if (result.out.size() > maxOutBytes) {
                result.tooLong = true;
                break;
            }
        }
        result.timedOut = !result.tooLong && process.state() != QProcess::NotRunning;
    }
    if (result.timedOut || result.tooLong) {
        process.kill();
        process.waitForFinished();
    } else if (process.exitStatus() == QProcess::NormalExit) {
        result.exitCode = process.exitCode();
    }
    result.out += process.readAllStandardOutput();
    result.err = process.readAllStandardError();
    return result;
}

} // namespace gw
