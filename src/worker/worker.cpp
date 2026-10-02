#include "worker.h"

#include "core/checkrunner.h"
#include "core/markers.h"
#include "core/statepaths.h"
#include "core/systemidentity.h"
#include "privilege.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QHash>
#include <QProcess>
#include <QTextStream>

#include <cstdio>

namespace gw {
namespace {

QString tr(const char *text) { return QCoreApplication::translate("gw::Worker", text); }

// zypper's exit codes in an apply step (design, What every part does the
// same way; man zypper, EXIT CODES).
bool zypperSucceeded(int code) { return code == 0 || (code >= 100 && code <= 103) || code == 106; }

} // namespace

Worker::Worker(const Catalogue &catalogue, FileReader files, QString stateDir)
    : m_catalogue(catalogue), m_files(std::move(files)), m_stateDir(std::move(stateDir))
{
}

bool Worker::stopRequested() const { return QFile::exists(stopFilePath(m_stateDir)); }

void Worker::say(const QString &line)
{
    // Written straight to the file descriptor: a closed reader (the window
    // gone) must not end the run, and SIGPIPE is ignored by Entry.
    const QByteArray bytes = (line + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    std::fflush(stdout);
    if (m_log.isOpen()) {
        m_log.write(bytes);
        m_log.flush();
    }
}

int Worker::runCommand(const QStringList &argv)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    QString pending;
    auto drain = [&] {
        pending += QString::fromUtf8(process.readAll());
        qsizetype nl;
        while ((nl = pending.indexOf(QLatin1Char('\n'))) >= 0) {
            say(pending.left(nl));
            pending.remove(0, nl + 1);
        }
    };
    QObject::connect(&process, &QProcess::readyRead, drain);
    QEventLoop loop; // keeps the keep-alive timer running during long steps
    QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
    QObject::connect(&process, &QProcess::errorOccurred, &loop, [&](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            loop.quit();
    });
    process.start(argv.first(), argv.mid(1));
    if (process.state() != QProcess::NotRunning || process.error() != QProcess::FailedToStart)
        loop.exec();
    drain();
    if (!pending.isEmpty())
        say(pending);
    if (process.error() == QProcess::FailedToStart)
        return -1;
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}

bool Worker::ensureRoot()
{
    if (m_auth == Auth::NotAsked) {
        m_auth = m_privilege.authenticate() ? Auth::Ok : Auth::Failed;
        if (m_auth == Auth::Ok) {
            say(formatMarker(QStringLiteral("AUTH"), {QStringLiteral("ok")}));
            m_privilege.startKeepAlive();
        }
    }
    return m_auth == Auth::Ok;
}

bool Worker::runStep(const QString &itemId, const Step &step, QString *detail)
{
    say(formatMarker(QStringLiteral("ACTION"), {itemId, step.label}));
    const QStringList argv = step.needsRoot ? Privilege::asRoot(step.argv) : step.argv;
    int code = runCommand(argv);
    if (step.tool == Step::Tool::Zypper) {
        if (code == 103) // zypper updated itself; run once more to finish
            code = runCommand(argv);
        if (code == 106)
            say(formatMarker(QStringLiteral("HINT"),
                              {itemId, tr("A software source could not be read and was skipped.")}));
        if (zypperSucceeded(code))
            return true;
    } else if (code == 0 || step.alsoOk.contains(code)) {
        return true;
    }
    *detail = tr("%1 failed (exit code %2).").arg(step.label).arg(code);
    return false;
}

int Worker::run(const QStringList &ids, const QHash<QString, QString> &values)
{
    (void)QDir().mkpath(m_stateDir + QStringLiteral("/logs"));
    // A stop file left by an earlier run must not stop this one.
    QFile::remove(stopFilePath(m_stateDir));
    m_log.setFileName(m_stateDir + QStringLiteral("/logs/run-")
                      + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))
                      + QStringLiteral(".log"));
    // Without a log the run goes on; standard output still carries it all.
    (void)m_log.open(QIODevice::WriteOnly | QIODevice::Append);

    const SystemIdentity system = readSystemIdentity(m_files);
    if (!system.supported()) {
        say(formatMarker(QStringLiteral("UNSUPPORTED"), {system.reason}));
        return Unsupported;
    }
    const CheckContext context(m_files);

    Selection selection;
    if (ids.isEmpty()) {
        selection = m_catalogue.defaultSelection(runChecks(m_catalogue.items(), context));
    } else {
        for (const QString &id : ids) {
            if (!m_catalogue.find(id)) {
                say(formatMarker(QStringLiteral("UNKNOWN_ITEM"), {id}));
                return Usage;
            }
            selection.insert(id);
        }
    }
    const QStringList order = m_catalogue.runOrder(selection);

    QHash<QString, Outcome> outcomes;
    int ok = 0;
    int failed = 0;
    bool stopped = false;
    for (qsizetype i = 0; i < order.size(); ++i) {
        if (stopRequested()) {
            stopped = true;
            break;
        }
        const Item *item = m_catalogue.find(order[i]);
        const QString id = item->id();
        say(formatMarker(QStringLiteral("STEP_BEGIN"),
                          {id, QString::number(i + 1), QString::number(order.size()), item->title()}));

        // An item already done, or not needed, is reported as such whatever
        // its dependencies did.
        const CheckResult recheck = item->check(context);
        if (recheck.state != CheckState::NotDone) {
            const Outcome o = recheck.state == CheckState::Done ? Outcome::SkippedDone
                : recheck.state == CheckState::NotNeeded       ? Outcome::SkippedNotNeeded
                                                               : Outcome::SkippedOther;
            const QString why = o == Outcome::SkippedDone ? tr("Already done.")
                : o == Outcome::SkippedNotNeeded            ? tr("Not needed here.")
                                                            : recheck.detail;
            outcomes.insert(id, o);
            say(formatMarker(QStringLiteral("STEP_END"), {id, QStringLiteral("skip"), why}));
            continue;
        }

        // A dependency that failed, was skipped for any reason but "already
        // done" or "not needed here", or is neither done nor in the list,
        // skips this item (design, Step results).
        QString blockedBy;
        for (const QString &dep : m_catalogue.dependencies(*item)) {
            const auto o = outcomes.constFind(dep);
            if (o != outcomes.constEnd()) {
                if (*o == Outcome::Failed || *o == Outcome::SkippedOther)
                    blockedBy = m_catalogue.find(dep)->title();
            } else if (!selection.contains(dep)
                       && m_catalogue.find(dep)->check(context).state == CheckState::NotDone) {
                blockedBy = m_catalogue.find(dep)->title();
            }
        }
        if (!blockedBy.isEmpty()) {
            outcomes.insert(id, Outcome::SkippedOther);
            say(formatMarker(QStringLiteral("STEP_END"),
                             {id, QStringLiteral("skip"), tr("Skipped: %1 did not complete.").arg(blockedBy)}));
            continue;
        }

        // An item that takes a value runs only with a valid one.
        const QString value = values.value(id);
        if (!item->valuePrompt().isEmpty() && (value.isEmpty() || !item->isValidValue(value))) {
            outcomes.insert(id, Outcome::SkippedOther);
            say(formatMarker(QStringLiteral("STEP_END"),
                             {id, QStringLiteral("skip"), tr("Skipped: no valid value was given.")}));
            continue;
        }

        QString detail;
        bool success = true;
        for (Step step : item->applySteps(system, context)) {
            step.argv.replaceInStrings(kValuePlaceholder, value);
            if (step.needsRoot && !ensureRoot())
                break;
            if (!runStep(id, step, &detail)) {
                success = false;
                break;
            }
        }
        if (m_auth == Auth::Failed) {
            say(formatMarker(QStringLiteral("AUTH"), {QStringLiteral("failed")}));
            m_privilege.stopKeepAlive();
            return AuthFailed;
        }
        outcomes.insert(id, success ? Outcome::Ok : Outcome::Failed);
        success ? ++ok : ++failed;
        say(formatMarker(QStringLiteral("STEP_END"),
                          {id, success ? QStringLiteral("ok") : QStringLiteral("fail"), detail}));
    }
    m_privilege.stopKeepAlive();

    say(formatMarker(QStringLiteral("DONE"),
                      {QString::number(ok), QString::number(failed), stopped ? QStringLiteral("1") : QStringLiteral("0")}));
    if (stopped)
        return Stopped;
    return failed > 0 ? Failed : Ok;
}

} // namespace gw
