#include "worker.h"

#include "carry/carry.h"
#include "carry/carrieditems.h"
#include "core/checkrunner.h"
#include "core/markers.h"
#include "core/statepaths.h"
#include "core/systemidentity.h"
#include "core/translations.h"
#include "drives/drives.h"
#include "github/github.h"
#include "privilege.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <utility>
#include <cstdio>

#include <unistd.h>

namespace gw {
namespace {

QString tr(const char *text) { return QCoreApplication::translate("gw::Worker", text); }

QStringList strings(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonArray values = value.toArray(); const QJsonValue v : values)
        out << v.toString();
    return out;
}

// zypper's exit codes in an apply step (design, What every part does the
// same way; man zypper, EXIT CODES).
bool zypperSucceeded(int code) { return code == 0 || (code >= 100 && code <= 103) || code == 106; }

// zypper -n answers a conflict by cancelling, exit 4; rpm refusing the
// transaction is 8. Either may come from one name among many (GRND-0086).
bool zypperClashed(int code) { return code == 4 || code == 8; }

// Past this many names failing alone, the failure is taken to be the
// system's, not theirs, and the rest are not tried one by one.
constexpr int kMostLoneClashes = 8;

// Below this much free space a failed step is taken to have filled the
// disk, or found it too full to start: one carried program can need
// hundreds of megabytes (GRND-0087's VM run needed 468 MB more).
constexpr qint64 kNearlyFull = qint64(1) << 30;

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
        // A command's line that looks like a marker is passed on as text.
        if (echo)
            say(line.startsWith(QLatin1String("@@")) ? QLatin1Char(' ') + line : line);
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
    // Closed with nothing to send too: tar reading an empty list of names
    // would otherwise wait for ever.
    if (process.waitForStarted()) {
        if (!input.isEmpty())
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
    int code = 0;
    if (step.tool == Step::Tool::Zypper) {
        code = runZypper(argv, step.input, lines, echo);
        Parts parts{itemId, kMostLoneClashes};
        sayIfSourceSkipped(itemId, code, &parts.sourceSkipped);
        // 4 and 8 are also the system's own failure, so a name that failed
        // alone clashed only if another part installed (GRND-0079 § 4.4).
        if (zypperSucceeded(code)
            || (step.namesFrom > 0 && zypperClashed(code)
                && installInParts(step, step.argv.mid(step.namesFrom), &code, &parts) && parts.installed))
            return true;
    } else {
        code = runCommand(argv, step.input, lines, echo);
        if (code == 0 || step.alsoOk.contains(code))
            return true;
    }
    *detail = tr("%1 failed (exit code %2).").arg(step.label).arg(code);
    if (const QString full = fullDisk(); !full.isEmpty())
        *detail += QLatin1Char(' ') + full;
    return false;
}

QString Worker::fullDisk()
{
    // Asked after the failure, so it holds in every language: zypper's and
    // tar's own words for a full disk are translated (GRND-0087).
    QStringList lines;
    const int code = runCommand({QStringLiteral("df"), QStringLiteral("--output=avail,target"),
                                 QStringLiteral("--block-size=1"), QStringLiteral("/"), QStringLiteral("/opt"),
                                 QStringLiteral("/usr"), QStringLiteral("/var"), QDir::homePath()},
                                {}, &lines, false);
    if (code < 0)
        return {};
    qint64 least = kNearlyFull;
    QString where;
    for (const QString &line : std::as_const(lines)) {
        const QString trimmed = line.trimmed();
        const qsizetype gap = trimmed.indexOf(QLatin1Char(' '));
        bool number = false;
        const qint64 avail = trimmed.left(gap).toLongLong(&number);
        if (number && gap > 0 && avail < least) {
            least = avail;
            where = trimmed.mid(gap + 1).trimmed();
        }
    }
    if (where.isEmpty())
        return {};
    return tr("The disk holding %1 is full: %2 free.")
        .arg(isolated(where), QLocale().formattedDataSize(least, 1, QLocale::DataSizeIecFormat));
}

bool Worker::installInParts(const Step &step, const QStringList &names, int *code, Parts *parts)
{
    // No names, or fewer arguments than namesFrom: nothing to split, and
    // halving nothing would never end.
    if (names.isEmpty() || step.namesFrom > step.argv.size())
        return false;
    if (names.size() == 1)
        return --parts->lonesLeft >= 0; // the carried item's check, run after, names it
    const qsizetype half = names.size() / 2;
    for (const QStringList &part : {names.first(half), names.mid(half)}) {
        const QStringList argv = step.argv.first(step.namesFrom) + part;
        *code = runZypper(step.needsRoot ? Privilege::asRoot(argv) : argv, step.input);
        sayIfSourceSkipped(parts->itemId, *code, &parts->sourceSkipped);
        if (zypperSucceeded(*code))
            parts->installed = true;
        else if (!(zypperClashed(*code) && installInParts(step, part, code, parts)))
            return false;
    }
    return true;
}

void Worker::sayIfSourceSkipped(const QString &itemId, int code, bool *said)
{
    if (code != 106 || *said)
        return;
    *said = true;
    say(formatMarker(QStringLiteral("HINT"), {itemId, tr("A software source could not be read and was skipped.")}));
}

int Worker::runZypper(const QStringList &argv, const QByteArray &input, QStringList *lines, bool echo)
{
    const int code = runCommand(argv, input, lines, echo);
    if (code == 103) // zypper updated itself; run once more to finish
        return runCommand(argv, input, lines, echo);
    return code;
}

void Worker::openLog()
{
    (void)makeStateDir(m_stateDir);
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
        // Except a done item given a value that differs from what is applied
        // now, such as a changed list of shared folders (GRND-0066 spec 4.7).
        const QString value = values.value(id);
        const bool changed = recheck.state == CheckState::Done && !recheck.currentValue.isEmpty()
            && !value.isEmpty() && value != recheck.currentValue;
        if (recheck.state != CheckState::NotDone && !changed) {
            const Outcome o = recheck.state == CheckState::Done ? Outcome::SkippedDone
                : recheck.state == CheckState::NotNeeded       ? Outcome::SkippedNotNeeded
                                                               : Outcome::SkippedOther;
            // A carried item done may still name what it left out, such as a
            // drive that is absent (GRND-0073 spec 4.9).
            const QString why = o == Outcome::SkippedDone
                ? (isCarriedId(id) && !recheck.detail.isEmpty() ? recheck.detail : tr("Already done."))
                : o == Outcome::SkippedNotNeeded ? tr("Not needed here.")
                                                 : recheck.detail;
            outcomes.insert(id, o);
            say(formatMarker(QStringLiteral("STEP_END"), {id, QStringLiteral("skip"), why}));
            continue;
        }

        // A dependency that failed, was skipped for any reason but "already
        // done" or "not needed here", or is neither done nor in the list,
        // skips this item (design, Step results).
        QString blockedBy;
        for (const QStringList deps = m_catalogue->dependencies(*item, recheck); const QString &dep : deps) {
            const Item *needed = m_catalogue->find(dep);
            if (!needed) { // not in this catalogue, so it cannot have completed
                blockedBy = dep;
                continue;
            }
            const auto o = outcomes.constFind(dep);
            if (o != outcomes.constEnd()) {
                if (*o == Outcome::Failed || *o == Outcome::SkippedOther)
                    blockedBy = needed->title();
            } else if (!selection.contains(dep)) {
                const CheckState state = needed->check(context).state;
                if (state != CheckState::Done && state != CheckState::NotNeeded)
                    blockedBy = needed->title();
            }
        }
        if (!blockedBy.isEmpty()) {
            outcomes.insert(id, Outcome::SkippedOther);
            say(formatMarker(QStringLiteral("STEP_END"),
                             {id, QStringLiteral("skip"), tr("Skipped: %1 did not complete.").arg(blockedBy)}));
            continue;
        }

        // An item that takes a value runs only with a valid one.
        if (!item->valuePrompt().isEmpty() && (value.isEmpty() || !item->isValidValue(value))) {
            outcomes.insert(id, Outcome::SkippedOther);
            say(formatMarker(QStringLiteral("STEP_END"),
                             {id, QStringLiteral("skip"), tr("Skipped: no valid value was given.")}));
            continue;
        }

        QString detail;
        bool success = true;
        for (Step &step : item->applyStepsFor(system, context, value)) {
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
            if (!after.detail.isEmpty())
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
    beginOne(id, tr("Preparing a drive"));
    auto end = [&](bool ok, const QString &detail) {
        m_privilege.stopKeepAlive();
        if (m_auth == Auth::Failed) {
            say(formatMarker(QStringLiteral("AUTH"), {QStringLiteral("failed")}));
            return int(AuthFailed);
        }
        return endOne(id, ok, detail);
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
        for (const QList<Drive> drives = readDrives(context); const Drive &d : drives) {
            if (d.path == planned->drive.path) // cppcheck-suppress useStlAlgorithm
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
    beginOne(id, tr("Saving the setup"));
    QString partial;
    // Where root writes a system part, before the user's own file takes it.
    const QString systemArchive = m_stateDir + QStringLiteral("/system.tar");
    auto end = [&](bool ok, QString detail) {
        m_privilege.stopKeepAlive();
        (void)QFile::remove(systemArchive);
        // The file at the output is whole or absent (INV-5).
        if (!ok && !partial.isEmpty() && !QFile::remove(partial) && QFile::exists(partial))
            detail += QLatin1Char(' ') + tr("A part-written file is left at %1; it can be deleted.").arg(partial);
        return endOne(id, ok, detail);
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

    // The firewall and printers, read as root: asked for once, and only when
    // switched on (GRND-0073 spec 4.5, INV-2). Listed first, since the
    // manifest names only a part with files to save.
    manifest->systemParts.clear();
    QList<SystemPart> parts;
    for (const QStringList kinds = strings(choices.value("systemKinds")); const QString &kind : kinds) {
        const std::optional<SystemPart> part = systemPartNamed(kind);
        if (!part)
            return end(false, tr("The list of what to save could not be read.") + QLatin1Char(' ') + nothing);
        parts << *part;
    }
    if (!parts.isEmpty() && !ensureRoot()) {
        (void)end(false, nothing);
        say(formatMarker(QStringLiteral("AUTH"), {QStringLiteral("failed")}));
        return AuthFailed;
    }
    QList<QPair<SystemPart, QByteArray>> toSave; // each part and its files, for tar's input
    QString systemDetail;
    for (const SystemPart &part : std::as_const(parts)) {
        QStringList found;
        const Step find{findSystemFilesArguments(part), true, Step::Tool::Generic,
                        tr("Reading %1").arg(isolated(part.name))};
        if (!runStep(id, find, &systemDetail, &found, false))
            return end(false, systemDetail + QLatin1Char(' ') + nothing);
        const QStringList files = saveableSystemFiles(part, found);
        if (files.isEmpty()) {
            say(formatMarker(QStringLiteral("HINT"), {id, tr("%1 has no settings of its own on this computer, so "
                                                             "there was nothing to save.").arg(isolated(part.name))}));
            continue;
        }
        QByteArray input;
        for (const QString &file : files)
            input += file.toUtf8() + '\0'; // cppcheck-suppress useStlAlgorithm
        toSave.append({part, input});
        manifest->systemParts << SettingsRow{part.name, {part.path}, part.secret, kSystemPartAllowance};
    }

    // tar writes no member for a path it cannot read at all, so the
    // manifest does not list one; the hint below still names its setting.
    const QStringList unreadable = unreadableSettings(*manifest, home);
    *manifest = withoutUnreadablePaths(*manifest, home);

    // The manifest, as it will be read back, goes first in the archive.
    QSaveFile list(m_stateDir + QLatin1Char('/') + kManifestName);
    if (!list.open(QIODevice::WriteOnly) || list.write(QJsonDocument(manifestJson(*manifest)).toJson()) < 0
        || !list.commit())
        return end(false, tr("The setup list could not be written to %1.").arg(m_stateDir) + QLatin1Char(' ') + nothing);

    // tar exits 1 when a file changed as it was read, a running browser say,
    // and leaves out a file it cannot read. Its only output is a warning
    // naming the file, in the user's language, so which kind each is comes
    // from reading the files here instead.
    partial = output + QStringLiteral(".partial");
    QString detail;
    QStringList warnings;
    const Step create{QStringList{QStringLiteral("tar")} + createArguments(partial, m_stateDir, home), false,
                      Step::Tool::Generic, tr("Writing the setup file"), {1}, createInput(*manifest)};
    if (!runStep(id, create, &detail, &warnings))
        return end(false, detail);
    // Each system part goes to an archive of its own as root, becomes the
    // user's, and joins the file (GRND-0073 spec 4.5).
    for (const auto &[part, input] : std::as_const(toSave)) {
        const QString label = tr("Saving %1").arg(isolated(part.name));
        const QList<Step> steps{
            {createSystemArguments(part, systemArchive), true, Step::Tool::Generic, label, {1}, input},
            {{QStringLiteral("chown"), QStringLiteral("%1:%2").arg(::getuid()).arg(::getgid()), systemArchive}, true,
             Step::Tool::Generic, label},
            {{QStringLiteral("tar"), QStringLiteral("--concatenate"), QStringLiteral("--file"), partial, systemArchive}, false,
             Step::Tool::Generic, label},
            {{QStringLiteral("rm"), QStringLiteral("-f"), QStringLiteral("--"), systemArchive}, false, Step::Tool::Generic, label}};
        for (const Step &step : steps) {
            if (!runStep(id, step, &detail)) // cppcheck-suppress useStlAlgorithm
                return end(false, detail + QLatin1Char(' ') + nothing);
        }
    }
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
        if (!runStep(id, step, &detail)) // cppcheck-suppress useStlAlgorithm
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

int Worker::installGithubTools()
{
    openLog();
    const SystemIdentity system = readSystemIdentity(m_files);
    if (!system.supported()) {
        say(formatMarker(QStringLiteral("UNSUPPORTED"), {system.reason}));
        return Unsupported;
    }
    const QString id = QStringLiteral("github-tools");
    const Step install = githubToolsStep();
    beginOne(id, install.label);
    if (!ensureRoot()) {
        say(formatMarker(QStringLiteral("AUTH"), {QStringLiteral("failed")}));
        return AuthFailed;
    }
    QString detail;
    const bool ok = runStep(id, install, &detail);
    m_privilege.stopKeepAlive();
    return endOne(id, ok, detail);
}

int Worker::copyGithub(const QString &choicesPath)
{
    openLog();
    const SystemIdentity system = readSystemIdentity(m_files);
    if (!system.supported()) {
        say(formatMarker(QStringLiteral("UNSUPPORTED"), {system.reason}));
        return Unsupported;
    }
    // Every check before any step (INV-3).
    QString why = tr("The list of projects to copy could not be read.");
    std::optional<GithubChoices> choices;
    if (QFile file(choicesPath); file.open(QIODevice::ReadOnly))
        choices = readGithubChoices(file.readAll(), &why);
    if (!choices) {
        say(formatMarker(QStringLiteral("SETUP_REFUSED"), {why}));
        return Usage;
    }
    const QString &folder = choices->folder;
    const auto present = [](const QString &path) {
        const QFileInfo info(path);
        return info.exists() || info.isSymLink(); // a link that leads nowhere is still there
    };
    QString detail;
    const QString setup = QStringLiteral("github-setup");
    if (!runStep(setup, makeGithubFolderStep(folder), &detail)) {
        say(formatMarker(QStringLiteral("SETUP_REFUSED"), {detail}));
        return Failed;
    }
    // So a later git push in a copied project works; the copies do not need it.
    if (!runStep(setup, setupGitStep(), &detail))
        say(formatMarker(QStringLiteral("HINT"), {setup, tr("git could not be set up to use your sign-in, so git push "
                                                            "may ask for it. Copying goes on.")}));

    int ok = 0;
    int failed = 0;
    bool stopped = false;
    const QStringList &repos = choices->repos;
    for (qsizetype i = 0; i < repos.size(); ++i) {
        if (stopRequested()) {
            stopped = true;
            break;
        }
        const QString &repo = repos[i];
        const QString id = QStringLiteral("github:") + repo;
        const QString name = repo.section(QLatin1Char('/'), 1);
        say(formatMarker(QStringLiteral("STEP_BEGIN"),
                         {id, QString::number(i + 1), QString::number(repos.size()), name}));
        if (present(githubDestination(folder, repo))) {
            say(formatMarker(QStringLiteral("STEP_END"),
                             {id, QStringLiteral("skip"), tr("Already there, so it was left as it is.")}));
            continue;
        }
        const QString partial = githubPartial(folder, repo);
        const QString leftover = tr("A part-copied folder is at %1. Delete it, then copy again.").arg(partial);
        detail.clear();
        bool success = false;
        if (present(partial)) {
            detail = leftover; // never deleted for the user
        } else {
            const QList<Step> steps = githubCopySteps(folder, repo);
            success = std::all_of(steps.cbegin(), steps.cend(), [&](const Step &step) { return runStep(id, step, &detail); });
            // mv -n moves nothing onto a folder that appeared meanwhile.
            if (present(partial)) {
                success = false;
                detail = detail.isEmpty() ? leftover : detail + QLatin1Char(' ') + leftover;
            }
        }
        success ? ++ok : ++failed;
        say(formatMarker(QStringLiteral("STEP_END"), {id, success ? QStringLiteral("ok") : QStringLiteral("fail"), detail}));
    }
    say(formatMarker(QStringLiteral("DONE"),
                     {QString::number(ok), QString::number(failed), stopped ? QStringLiteral("1") : QStringLiteral("0")}));
    if (stopped)
        return Stopped;
    return failed > 0 ? Failed : Ok;
}

} // namespace gw
