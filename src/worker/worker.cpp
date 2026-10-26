#include "worker.h"

#include "carry/carry.h"
#include "carry/carrieditems.h"
#include "core/checkrunner.h"
#include "core/markers.h"
#include "core/statepaths.h"
#include "core/systemidentity.h"
#include "drives/drives.h"
#include "privilege.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <utility>
#include <cstdio>

namespace gw {
namespace {

QString tr(const char *text) { return QCoreApplication::translate("gw::Worker", text); }

// zypper's exit codes in an apply step (design, What every part does the
// same way; man zypper, EXIT CODES).
bool zypperSucceeded(int code) { return code == 0 || (code >= 100 && code <= 103) || code == 106; }

} // namespace

Worker::Worker(const Catalogue &catalogue, FileReader files, QString stateDir)
    : m_catalogue(&catalogue), m_files(std::move(files)), m_stateDir(std::move(stateDir))
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

int Worker::runCommand(const QStringList &argv, const QByteArray &input, QStringList *lines, bool echo)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    QString pending;
    auto take = [&](const QString &line) {
        if (lines)
            *lines << line;
        if (echo)
            say(line);
    };
    auto drain = [&] {
        pending += QString::fromUtf8(process.readAll());
        qsizetype nl;
        while ((nl = pending.indexOf(QLatin1Char('\n'))) >= 0) {
            take(pending.left(nl));
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
    if (!input.isEmpty() && process.waitForStarted()) {
        process.write(input);
        process.closeWriteChannel();
    }
    if (process.state() != QProcess::NotRunning || process.error() != QProcess::FailedToStart)
        loop.exec();
    drain();
    if (!pending.isEmpty())
        take(pending);
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

bool Worker::runStep(const QString &itemId, const Step &step, QString *detail, QStringList *lines, bool echo)
{
    say(formatMarker(QStringLiteral("ACTION"), {itemId, step.label}));
    const QStringList argv = step.needsRoot ? Privilege::asRoot(step.argv) : step.argv;
    int code = runCommand(argv, step.input, lines, echo);
    if (step.tool == Step::Tool::Zypper) {
        if (code == 103) // zypper updated itself; run once more to finish
            code = runCommand(argv, step.input);
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

void Worker::openLog()
{
    (void)QDir().mkpath(m_stateDir + QStringLiteral("/logs"));
    // A stop file left by an earlier run must not stop this one.
    QFile::remove(stopFilePath(m_stateDir));
    m_log.setFileName(m_stateDir + QStringLiteral("/logs/run-")
                      + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))
                      + QStringLiteral(".log"));
    // Without a log the run goes on; standard output still carries it all.
    (void)m_log.open(QIODevice::WriteOnly | QIODevice::Append);
}

int Worker::run(const QStringList &ids, const QHash<QString, QString> &values)
{
    openLog();

    const SystemIdentity system = readSystemIdentity(m_files);
    if (!system.supported()) {
        say(formatMarker(QStringLiteral("UNSUPPORTED"), {system.reason}));
        return Unsupported;
    }
    const CheckContext context(m_files);

    Selection selection;
    if (ids.isEmpty()) {
        selection = m_catalogue->defaultSelection(runChecks(m_catalogue->items(), context));
    } else {
        for (const QString &id : ids) {
            if (!m_catalogue->find(id)) {
                say(formatMarker(QStringLiteral("UNKNOWN_ITEM"), {id}));
                return Usage;
            }
            selection.insert(id);
        }
    }
    const QStringList order = m_catalogue->runOrder(selection);

    QHash<QString, Outcome> outcomes;
    int ok = 0;
    int failed = 0;
    bool stopped = false;
    for (qsizetype i = 0; i < order.size(); ++i) {
        if (stopRequested()) {
            stopped = true;
            break;
        }
        const Item *item = m_catalogue->find(order[i]);
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
        for (const QString &dep : m_catalogue->dependencies(*item)) {
            const auto o = outcomes.constFind(dep);
            if (o != outcomes.constEnd()) {
                if (*o == Outcome::Failed || *o == Outcome::SkippedOther)
                    blockedBy = m_catalogue->find(dep)->title();
            } else if (!selection.contains(dep)
                       && m_catalogue->find(dep)->check(context).state == CheckState::NotDone) {
                blockedBy = m_catalogue->find(dep)->title();
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
        // What a carried item could not put back, such as a program this
        // system lacks, is named (GRND-0079 spec 4.4, INV-4).
        if (success && isCarriedId(id)) {
            const CheckResult after = item->check(context);
            if (after.state != CheckState::Done && !after.detail.isEmpty())
                say(formatMarker(QStringLiteral("HINT"), {id, after.detail}));
            if (id == kCarriedSettingsId)
                say(formatMarker(QStringLiteral("HINT"),
                                 {id, tr("Log out and back in, so apps that are open read the restored settings.")}));
        }
    }
    m_privilege.stopKeepAlive();

    // zypper's word for it is in its output alone, under the details; its
    // own check for scripts says it by exit code (GRND-0048).
    if (ok > 0 && runCommand({QStringLiteral("zypper"), QStringLiteral("needs-rebooting")}) == 102)
        say(formatMarker(QStringLiteral("HINT"),
                          {QString(), tr("Core parts of the system were updated. Restart the computer when you "
                                         "can, so they take effect.")}));

    say(formatMarker(QStringLiteral("DONE"),
                      {QString::number(ok), QString::number(failed), stopped ? QStringLiteral("1") : QStringLiteral("0")}));
    if (stopped)
        return Stopped;
    return failed > 0 ? Failed : Ok;
}

int Worker::prepareDrive(const QString &layoutPath)
{
    openLog();
    const SystemIdentity system = readSystemIdentity(m_files);
    if (!system.supported()) {
        say(formatMarker(QStringLiteral("UNSUPPORTED"), {system.reason}));
        return Unsupported;
    }
    const CheckContext context(m_files);
    const QString id = QStringLiteral("prepare-drive");
    say(formatMarker(QStringLiteral("STEP_BEGIN"), {id, QStringLiteral("1"), QStringLiteral("1"), tr("Preparing a drive")}));
    auto end = [&](bool ok, const QString &detail) {
        m_privilege.stopKeepAlive();
        if (m_auth == Auth::Failed) {
            say(formatMarker(QStringLiteral("AUTH"), {QStringLiteral("failed")}));
            return int(AuthFailed);
        }
        say(formatMarker(QStringLiteral("STEP_END"), {id, ok ? QStringLiteral("ok") : QStringLiteral("fail"), detail}));
        say(formatMarker(QStringLiteral("DONE"),
                         {ok ? QStringLiteral("1") : QStringLiteral("0"), ok ? QStringLiteral("0") : QStringLiteral("1"),
                          QStringLiteral("0")}));
        return int(ok ? Ok : Failed);
    };
    const QString nothing = tr("Nothing was written.");
    QString why = tr("The layout file could not be read.");
    std::optional<LayoutFile> planned;
    if (QFile file(layoutPath); file.open(QIODevice::ReadOnly))
        planned = readLayoutFile(file.readAll(), &why);
    if (!planned)
        return end(false, why + QLatin1Char(' ') + nothing);
    const Layout &layout = planned->layout;
    auto driveNow = [&]() -> std::optional<Drive> {
        for (const Drive &d : readDrives(context)) {
            if (d.path == planned->drive.path)
                return d;
        }
        return std::nullopt;
    };

    // Only the drive the user saw (INV-3), with a layout that is valid for
    // it as it is now (INV-4).
    const std::optional<Drive> before = driveNow();
    if (!before || before->size != planned->drive.size || before->model != planned->drive.model
        || before->serial != planned->drive.serial || before->ptuuid != planned->drive.ptuuid)
        return end(false, tr("This is not the drive you chose: it may have been changed or unplugged.")
                              + QLatin1Char(' ') + nothing);
    if (layout.firmware != readFirmware(context))
        return end(false, tr("The layout was made for a computer that starts differently.") + QLatin1Char(' ') + nothing);
    if (const QString bad = validate(layout, *before); !bad.isEmpty())
        return end(false, bad + QLatin1Char(' ') + nothing);

    const QList<qint64> sizes = partitionSizes(layout, *before);
    QString detail;
    if (!ensureRoot())
        return end(false, {});
    const Step write{sfdiskArguments(layout, *before), true, Step::Tool::Generic,
                     tr("Writing the partition table"), {}, sfdiskScript(layout, *before)};
    if (!runStep(id, write, &detail))
        return end(false, detail + QLatin1Char(' ') + tr("The drive may now be left without partitions; prepare it again."));

    // The new partitions' paths, by order of start; then their filesystems.
    (void)runCommand({QStringLiteral("udevadm"), QStringLiteral("settle")});
    std::optional<Drive> after = driveNow();
    QList<Partition> added = after ? addedPartitions(layout, *before, *after) : QList<Partition>();
    if (added.size() != layout.partitions.size())
        return end(false, tr("The drive does not show the new partitions."));
    for (qsizetype i = 0; i < added.size(); ++i) {
        const Format format = formatFor(layout.partitions[i].use);
        if (format.argv.isEmpty())
            continue;
        const Step mkfs{format.argv + QStringList{added[i].path}, true, Step::Tool::Generic,
                        tr("Formatting %1").arg(added[i].path)};
        if (!runStep(id, mkfs, &detail))
            return end(false, detail + QLatin1Char(' ') + tr("The drive is partitioned but not fully formatted."));
    }

    // It ends ok only when the drive reads back as planned (INV-6).
    (void)runCommand({QStringLiteral("udevadm"), QStringLiteral("settle")});
    after = driveNow();
    added = after ? addedPartitions(layout, *before, *after) : QList<Partition>();
    bool matches = added.size() == layout.partitions.size();
    for (qsizetype i = 0; matches && i < added.size(); ++i) {
        const Format format = formatFor(layout.partitions[i].use);
        matches = qAbs(added[i].size - sizes[i]) <= kMiB && added[i].fstype == format.fstype
                  && added[i].label == format.label;
    }
    if (!matches)
        return end(false, tr("The drive does not read back as planned."));

    // What to pick in the installer, read off the drive as it now is (spec 4.6).
    say(formatMarker(QStringLiteral("HINT"),
                     {id, tr("In the installer, choose to use the existing partitions rather than its own "
                             "proposal, and set each one as below.")}));
    for (qsizetype i = 0; i < added.size(); ++i)
        say(formatMarker(QStringLiteral("HINT"), {id, installerAdvice(added[i], layout.partitions[i].use)}));
    return end(true, {});
}

int Worker::saveSetup(const QString &choicesPath)
{
    openLog();
    const SystemIdentity system = readSystemIdentity(m_files);
    if (!system.supported()) {
        say(formatMarker(QStringLiteral("UNSUPPORTED"), {system.reason}));
        return Unsupported;
    }
    const QString id = QStringLiteral("save-setup");
    say(formatMarker(QStringLiteral("STEP_BEGIN"), {id, QStringLiteral("1"), QStringLiteral("1"), tr("Saving the setup")}));
    QString partial;
    auto end = [&](bool ok, QString detail) {
        // The file at the output is whole or absent (INV-5).
        if (!ok && !partial.isEmpty() && !QFile::remove(partial) && QFile::exists(partial))
            detail += QLatin1Char(' ') + tr("A part-written file is left at %1; it can be deleted.").arg(partial);
        say(formatMarker(QStringLiteral("STEP_END"), {id, ok ? QStringLiteral("ok") : QStringLiteral("fail"), detail}));
        say(formatMarker(QStringLiteral("DONE"),
                         {ok ? QStringLiteral("1") : QStringLiteral("0"), ok ? QStringLiteral("0") : QStringLiteral("1"),
                          QStringLiteral("0")}));
        return int(ok ? Ok : Failed);
    };
    const QString nothing = tr("Nothing was saved.");

    // Every check before anything is written (spec 4.5, step 1; INV-3).
    QJsonObject choices;
    if (QFile file(choicesPath); file.open(QIODevice::ReadOnly))
        choices = QJsonDocument::fromJson(file.readAll()).object();
    QString why = tr("The list of what to save could not be read.");
    QStringList skipped;
    std::optional<Manifest> manifest;
    if (choices.value("manifest").isObject())
        manifest = readManifest(choices.value("manifest").toObject(), &why, &skipped);
    if (!manifest)
        return end(false, why + QLatin1Char(' ') + nothing);
    if (!skipped.isEmpty())
        return end(false, skipped.first() + QLatin1Char(' ') + nothing);
    const QString output = choices.value("output").toString();
    const QFileInfo outputInfo(output);
    if (output.isEmpty() || !outputInfo.isAbsolute() || !outputInfo.dir().exists())
        return end(false, tr("The folder to save in was not found.") + QLatin1Char(' ') + nothing);
    const QString home = QDir::homePath();
    for (const SettingsRow &row : std::as_const(manifest->settings)) {
        for (const QString &path : row.paths) {
            const QFileInfo info(home + QLatin1Char('/') + path);
            if (!info.exists() && !info.isSymLink())
                return end(false, tr("%1 is no longer in the home folder.").arg(path) + QLatin1Char(' ') + nothing);
        }
    }

    // The manifest, as it will be read back, goes first in the archive.
    QSaveFile list(m_stateDir + QLatin1Char('/') + kManifestName);
    if (!list.open(QIODevice::WriteOnly) || list.write(QJsonDocument(manifestJson(*manifest)).toJson()) < 0
        || !list.commit())
        return end(false, tr("The setup list could not be written to %1.").arg(m_stateDir) + QLatin1Char(' ') + nothing);

    // tar exits 1 when a file changed as it was read, a running browser say,
    // and leaves out a file it cannot read. Its only output is a warning
    // naming the file, in the user's language, so which kind each is comes
    // from reading the files here instead.
    const QStringList unreadable = unreadableSettings(*manifest, home);
    partial = output + QStringLiteral(".partial");
    QString detail;
    QStringList warnings;
    const Step create{QStringList{QStringLiteral("tar")} + createArguments(partial, m_stateDir, home), false,
                      Step::Tool::Generic, tr("Writing the setup file"), {1}, createInput(*manifest)};
    if (!runStep(id, create, &detail, &warnings))
        return end(false, detail);
    if (!unreadable.isEmpty())
        say(formatMarker(QStringLiteral("HINT"),
                         {id, tr("Some files in these settings could not be read, so they were left out: %1.")
                                  .arg(unreadable.join(QStringLiteral(", ")))}));
    // A setting with an unreadable file is named once, above.
    QStringList changed;
    for (const SettingsRow &row : std::as_const(manifest->settings)) {
        for (const QString &path : row.paths) {
            if (!changed.contains(row.name) && !unreadable.contains(row.name)
                && std::any_of(warnings.cbegin(), warnings.cend(), [&](const QString &w) { return w.contains(path); }))
                changed << row.name;
        }
    }
    if (!changed.isEmpty())
        say(formatMarker(QStringLiteral("HINT"),
                         {id, tr("These settings changed while they were saved, perhaps because the app was open: %1. "
                                 "Close it and save again to be sure.")
                                  .arg(changed.join(QStringLiteral(", ")))}));

    // It ends ok only when the file lists what the manifest names (INV-5).
    QStringList members;
    const Step check{QStringList{QStringLiteral("tar")} + listArguments(partial), false, Step::Tool::Generic,
                     tr("Checking the setup file")};
    if (!runStep(id, check, &detail, &members, false))
        return end(false, detail);
    if (const QString problem = membersProblem(members, *manifest); !problem.isEmpty())
        return end(false, problem);

    const Step move{{QStringLiteral("mv"), QStringLiteral("-f"), QStringLiteral("--"), partial, output}, false,
                    Step::Tool::Generic, tr("Putting the setup file in place")};
    if (!runStep(id, move, &detail))
        return end(false, detail);
    say(formatMarker(QStringLiteral("HINT"), {id, tr("Saved to %1.").arg(output)}));
    return end(true, {});
}

int Worker::runFromSetup(const QString &choicesPath, const QStringList &ids, const QHash<QString, QString> &values)
{
    // Nothing from the choices file reaches a command unless the saved
    // file holds it too (INV-2).
    auto refuse = [&](const QString &why) {
        openLog();
        say(formatMarker(QStringLiteral("SETUP_REFUSED"), {why}));
        return int(Usage);
    };
    QJsonObject choices;
    if (QFile file(choicesPath); file.open(QIODevice::ReadOnly))
        choices = QJsonDocument::fromJson(file.readAll()).object();
    QString why = tr("The list of what to restore could not be read.");
    QStringList skipped;
    std::optional<Manifest> chosen;
    if (choices.value("manifest").isObject())
        chosen = readManifest(choices.value("manifest").toObject(), &why, &skipped);
    if (!chosen)
        return refuse(why);
    if (!skipped.isEmpty())
        return refuse(skipped.first());
    const QString file = choices.value("file").toString();
    QStringList fileSkipped;
    const std::optional<Manifest> saved = readSetupFile(CheckContext(m_files), file, &why, &fileSkipped);
    if (!saved)
        return refuse(why);
    if (const QString lacks = notInFile(*chosen, *saved); !lacks.isEmpty())
        return refuse(lacks);

    const CarriedItems carried = carriedItems(*chosen, file, *m_catalogue);
    const Catalogue all = withCarried(*m_catalogue, carried);
    const Catalogue *own = std::exchange(m_catalogue, &all);
    const int code = run(ids, values);
    m_catalogue = own;
    return code;
}

void Worker::beginOne(const QString &id, const QString &title)
{
    say(formatMarker(QStringLiteral("STEP_BEGIN"), {id, QStringLiteral("1"), QStringLiteral("1"), title}));
}

int Worker::endOne(const QString &id, bool ok, const QString &detail)
{
    say(formatMarker(QStringLiteral("STEP_END"), {id, ok ? QStringLiteral("ok") : QStringLiteral("fail"), detail}));
    say(formatMarker(QStringLiteral("DONE"),
                     {ok ? QStringLiteral("1") : QStringLiteral("0"), ok ? QStringLiteral("0") : QStringLiteral("1"),
                      QStringLiteral("0")}));
    return ok ? Ok : Failed;
}

int Worker::removeSecrets(const QString &file)
{
    openLog();
    const QString id = QStringLiteral("remove-secrets");
    beginOne(id, tr("Removing saved passwords and keys"));
    const CheckContext context(m_files);
    QString why;
    QStringList skipped;
    const std::optional<Manifest> manifest = readSetupFile(context, file, &why, &skipped);
    if (!manifest)
        return endOne(id, false, why);
    const Manifest kept = withoutSecrets(*manifest);
    if (kept == *manifest)
        return endOne(id, true, {});

    // tar --delete edits the file in place (GRND-0078 INV-4), so a step
    // that fails may leave it part done.
    const QString partDone = tr("The file may now be unreadable. Your settings are already on this computer, so "
                                "it can be deleted.");
    QString detail;
    QStringList members;
    const Step list{QStringList{QStringLiteral("tar")} + listArguments(file), false, Step::Tool::Generic,
                    tr("Reading the saved file")};
    if (!runStep(id, list, &detail, &members, false))
        return endOne(id, false, detail);
    QSaveFile text(m_stateDir + QLatin1Char('/') + kManifestName);
    if (!text.open(QIODevice::WriteOnly) || text.write(QJsonDocument(manifestJson(kept)).toJson()) < 0 || !text.commit())
        return endOne(id, false, tr("The setup list could not be written to %1.").arg(m_stateDir));
    const QString label = tr("Removing them from the file");
    const QList<Step> steps{
        {QStringList{QStringLiteral("tar"), QStringLiteral("--delete"), QStringLiteral("--file"), file}
             + secretMembers(members, *manifest),
         false, Step::Tool::Generic, label},
        {{QStringLiteral("tar"), QStringLiteral("--delete"), QStringLiteral("--file"), file, kManifestName}, false,
         Step::Tool::Generic, label},
        {{QStringLiteral("tar"), QStringLiteral("--append"), QStringLiteral("--file"), file, QStringLiteral("-C"),
          m_stateDir, kManifestName},
         false, Step::Tool::Generic, label}};
    for (const Step &step : steps) {
        if (!runStep(id, step, &detail))
            return endOne(id, false, detail + QLatin1Char(' ') + partDone);
    }

    // It ends ok only when no member is under a secret setting (INV-9).
    members.clear();
    if (!runStep(id, list, &detail, &members, false))
        return endOne(id, false, detail + QLatin1Char(' ') + partDone);
    if (!secretMembers(members, *manifest).isEmpty())
        return endOne(id, false, tr("Some saved passwords or keys are still in the file.") + QLatin1Char(' ') + partDone);
    if (const QString problem = membersProblem(members, kept); !problem.isEmpty())
        return endOne(id, false, problem + QLatin1Char(' ') + partDone);
    say(formatMarker(QStringLiteral("HINT"), {id, tr("The saved passwords and keys were removed from %1.").arg(file)}));
    return endOne(id, true, {});
}

int Worker::deleteSetupFile(const QString &file)
{
    openLog();
    const QString id = QStringLiteral("delete-setup-file");
    beginOne(id, tr("Deleting the saved file"));
    QString detail;
    const Step remove{{QStringLiteral("rm"), QStringLiteral("-f"), QStringLiteral("--"), file}, false,
                      Step::Tool::Generic, tr("Deleting the saved file")};
    if (!runStep(id, remove, &detail))
        return endOne(id, false, detail);
    say(formatMarker(QStringLiteral("HINT"), {id, tr("Deleted %1.").arg(file)}));
    return endOne(id, true, {});
}

} // namespace gw
