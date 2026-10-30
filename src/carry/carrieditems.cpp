#include "carrieditems.h"

#include "core/translations.h"
#include "items/service.h"

#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

#include <unistd.h>

namespace gw {
namespace {

constexpr int kLongTimeoutMs = 10 * 60 * 1000; // rpm over hundreds of names, tar over a large file
const QString kReposDir = QStringLiteral("/etc/zypp/repos.d");
constexpr qsizetype kNamesShown = 10;
const QString kFstabPath = QStringLiteral("/etc/fstab");

// True when every line tar --diff printed is an owner or a mode, in the
// C locale the runner sets. A missing file is named on stderr alone.
bool onlyOwnerOrModeDiffers(const CommandResult &diff)
{
    if (!diff.err.trimmed().isEmpty())
        return false;
    static const QStringList kinds{QStringLiteral(": Uid differs"), QStringLiteral(": Gid differs"),
                                   QStringLiteral(": Mode differs")};
    const QStringList lines = QString::fromUtf8(diff.out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    return !lines.isEmpty() && std::all_of(lines.cbegin(), lines.cend(), [](const QString &line) {
        return std::any_of(kinds.cbegin(), kinds.cend(), [&](const QString &kind) { return line.endsWith(kind); });
    });
}

// Up to ten names, then how many more, so a row on a fresh install does
// not list every program.
QString namesText(const QStringList &all)
{
    QStringList shown;
    for (const QStringList first = all.mid(0, kNamesShown); const QString &name : first)
        shown << isolated(name);
    QString text = shown.join(QStringLiteral(", "));
    if (all.size() > kNamesShown)
        text = QCoreApplication::translate("gw::Carry", "%1 and %n more", nullptr, int(all.size() - kNamesShown))
                   .arg(text);
    return text;
}

CheckResult missing(const QStringList &absent)
{
    return {CheckState::NotDone, QCoreApplication::translate("gw::Carry", "Not installed: %1").arg(namesText(absent))};
}

class SourceItem : public Item
{
public:
    explicit SourceItem(ZypperSource source) : m_source(std::move(source)) {}
    QString id() const override { return kCarriedSourcePrefix + m_source.alias; }
    Level level() const override { return Level::NiceToHave; }
    QString title() const override
    {
        return QCoreApplication::translate("gw::Carry", "Software source: %1").arg(isolated(m_source.name));
    }
    QString applySentence() const override
    {
        return QCoreApplication::translate("gw::Carry", "Adds this source as your old computer had it, at %1, and "
                                                        "trusts its signing key.")
            .arg(isolated(m_source.baseurl));
    }
    // Done by alias or by baseurl, so a source Groundwork's own item added
    // under another alias is not added twice (INV-7). Couldn't tell when a
    // file there cannot be read, since it may hold this source, or when
    // the step's <alias>.repo already holds another, which the move would
    // replace (GRND-0089).
    CheckResult check(const CheckContext &context) const override
    {
        const QString fileName = m_source.alias + QStringLiteral(".repo");
        QStringList unreadable;
        bool taken = false;
        for (const QStringList names = context.entries(kReposDir); const QString &name : names) {
            if (!name.endsWith(QLatin1String(".repo")))
                continue;
            const std::optional<QByteArray> text = context.readFile(kReposDir + QLatin1Char('/') + name);
            const std::optional<QList<ZypperSource>> sources = text ? parseRepoFile(*text) : std::nullopt;
            if (!sources)
                unreadable << name;
            taken = taken || name == fileName;
            const QList<ZypperSource> found = sources.value_or(QList<ZypperSource>());
            if (std::any_of(found.cbegin(), found.cend(), [&](const ZypperSource &s) {
                    return s.alias == m_source.alias || s.baseurl == m_source.baseurl;
                }))
                return {CheckState::Done, {}};
        }
        if (!unreadable.isEmpty())
            return {CheckState::CouldNotTell,
                    QCoreApplication::translate("gw::Carry", "Groundwork could not read %1 in %2.")
                        .arg(namesText(unreadable), isolated(kReposDir))};
        if (taken)
            return {CheckState::CouldNotTell,
                    QCoreApplication::translate("gw::Carry", "%1 already holds another software source.")
                        .arg(isolated(kReposDir + QLatin1Char('/') + fileName))};
        return {CheckState::NotDone, {}};
    }
    // Refreshed from a folder of its own first, and moved into place only
    // when that works, so a source that fails is never added (spec 4.3).
    QList<Step> applySteps(const SystemIdentity &, const CheckContext &) const override
    {
        const QString staged = kStagedReposDir + QLatin1Char('/') + m_source.alias + QStringLiteral(".repo");
        const QString label = QCoreApplication::translate("gw::Carry", "Adding the software source %1")
                                  .arg(isolated(m_source.name));
        return {makeDirectoriesStep({kStagedReposDir}, label),
                writeFileStep(staged, repoFileText(m_source), label),
                {{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("-D"), kStagedReposDir,
                  QStringLiteral("--gpg-auto-import-keys"), QStringLiteral("refresh"), m_source.alias},
                 true, Step::Tool::Zypper,
                 QCoreApplication::translate("gw::Carry", "Reading the software list of %1").arg(isolated(m_source.name))},
                {{QStringLiteral("mv"), QStringLiteral("-f"), QStringLiteral("--"), staged,
                  kReposDir + QLatin1Char('/') + m_source.alias + QStringLiteral(".repo")},
                 true, Step::Tool::Generic, label}};
    }

private:
    ZypperSource m_source;
};

class PackagesItem : public Item
{
public:
    explicit PackagesItem(QStringList packages) : m_packages(std::move(packages)) {}
    QString id() const override { return kCarriedPackagesId; }
    Level level() const override { return Level::NiceToHave; }
    QString title() const override { return QCoreApplication::translate("gw::Carry", "Programs from your saved file"); }
    QString applySentence() const override
    {
        return QCoreApplication::translate("gw::Carry", "Installs the programs you had. Any that cannot be installed "
                                                        "is skipped and named at the end.");
    }
    bool installsPackages() const override { return true; }
    // rpm -q exits with the number not installed, and names each one.
    CheckResult check(const CheckContext &context) const override
    {
        const CommandResult r =
            context.run(QStringList{QStringLiteral("rpm"), QStringLiteral("-q")} + m_packages, kLongTimeoutMs);
        if (!r.started || r.timedOut)
            return {CheckState::CouldNotTell,
                    QCoreApplication::translate("gw::Carry", "The installed programs could not be listed.")};
        if (r.exitCode == 0)
            return {CheckState::Done, {}};
        QStringList absent;
        for (const QString &name : m_packages) {
            if (r.out.contains(QStringLiteral("package %1 is not installed").arg(name).toUtf8()))
                absent << name;
        }
        if (absent.isEmpty())
            return {CheckState::CouldNotTell,
                    QCoreApplication::translate("gw::Carry", "The installed programs could not be listed.")};
        return missing(absent);
    }
    // One unknown name would make zypper install nothing (spec 4.4); one
    // that clashes still does, so the Worker then installs in parts.
    QList<Step> applySteps(const SystemIdentity &, const CheckContext &) const override
    {
        const QStringList install{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("--ignore-unknown"),
                                  QStringLiteral("install")};
        return {{install + m_packages, true, Step::Tool::Zypper,
                 QCoreApplication::translate("gw::Carry", "Installing your programs"), {}, {}, install.size()}};
    }

private:
    QStringList m_packages;
};

class FlatpaksItem : public Item
{
public:
    FlatpaksItem(QList<FlatpakApp> apps, bool needsFlathub) : m_apps(std::move(apps)), m_needsFlathub(needsFlathub) {}
    QString id() const override { return kCarriedFlatpaksId; }
    Level level() const override { return Level::NiceToHave; }
    QString title() const override { return QCoreApplication::translate("gw::Carry", "Flatpak apps from your saved file"); }
    QString applySentence() const override
    {
        return QCoreApplication::translate("gw::Carry", "Installs the Flatpak apps you had, one at a time. Any that "
                                                        "cannot be installed is named at the end.");
    }
    QStringList dependsOn() const override { return m_needsFlathub ? QStringList{QStringLiteral("flathub")} : QStringList(); }
    CheckResult check(const CheckContext &context) const override
    {
        QStringList absent;
        const std::optional<QList<FlatpakApp>> apps = absentApps(context, &absent);
        if (!apps)
            return {CheckState::CouldNotTell, QCoreApplication::translate("gw::Carry", "Flatpak could not list its apps.")};
        return apps->isEmpty() ? CheckResult{CheckState::Done, {}} : missing(absent);
    }
    QList<Step> applySteps(const SystemIdentity &, const CheckContext &context) const override
    {
        QStringList absent;
        QList<Step> steps;
        auto install = [&](const FlatpakApp &app) {
            steps.append({{QStringLiteral("flatpak"), QStringLiteral("install"), QStringLiteral("-y"),
                           QStringLiteral("--noninteractive"), QStringLiteral("--") + app.installation, app.remote, app.app},
                          app.installation == QLatin1String("system"), Step::Tool::Generic,
                          QCoreApplication::translate("gw::Carry", "Installing %1").arg(isolated(app.app)), {1}});
        };
        // flatpak exits 1 for an app it cannot install; the others still go
        // on, and the check run after names it (spec 4.4).
        const QList<FlatpakApp> apps = absentApps(context, &absent).value_or(m_apps);
        for (const FlatpakApp &app : apps)
            if (app.installation == QLatin1String("system"))
                install(app);
        // The flathub item adds Flathub system-wide only, so an app just for
        // the user needs it in the user's own installation (spec 4.2). It
        // comes after the apps for everyone, so its failing cannot stop them.
        bool flathubForUser = false;
        for (const FlatpakApp &app : apps) {
            if (app.installation == QLatin1String("system"))
                continue;
            if (app.remote == QLatin1String("flathub") && !std::exchange(flathubForUser, true))
                steps.append({{QStringLiteral("flatpak"), QStringLiteral("remote-add"), QStringLiteral("--user"),
                               QStringLiteral("--if-not-exists"), QStringLiteral("flathub"),
                               QStringLiteral("https://dl.flathub.org/repo/flathub.flatpakrepo")},
                              false, Step::Tool::Generic,
                              QCoreApplication::translate("gw::Carry", "Adding Flathub for you")});
            install(app);
        }
        return steps;
    }

private:
    // The kept apps not installed for anyone; nothing when Flatpak could not
    // list them. Flatpak not installed at all means none is. An app a
    // Groundwork item installed for everyone is not installed again just for
    // the user (GRND-0100).
    std::optional<QList<FlatpakApp>> absentApps(const CheckContext &context, QStringList *names) const
    {
        const CommandResult r = context.run({QStringLiteral("flatpak"), QStringLiteral("list"), QStringLiteral("--app"),
                                             QStringLiteral("--columns=application,installation")});
        if (r.started && !r.ok())
            return std::nullopt;
        const QStringList lines = QString::fromUtf8(r.out).split(QLatin1Char('\n'));
        QList<FlatpakApp> absent;
        for (const FlatpakApp &app : m_apps) {
            const QString listed = app.app + QLatin1Char('\t');
            if (std::none_of(lines.cbegin(), lines.cend(), [&](const QString &line) { return line.startsWith(listed); })) {
                absent << app;
                *names << app.app;
            }
        }
        return absent;
    }

    QList<FlatpakApp> m_apps;
    bool m_needsFlathub;
};

class SettingsItem : public Item
{
public:
    SettingsItem(QList<SettingsRow> settings, QString file, QString home)
        : m_settings(std::move(settings)), m_file(std::move(file)), m_home(std::move(home)) {}
    QString id() const override { return kCarriedSettingsId; }
    Level level() const override { return Level::NiceToHave; }
    QString title() const override { return QCoreApplication::translate("gw::Carry", "Settings from your saved file"); }
    QString applySentence() const override
    {
        return QCoreApplication::translate("gw::Carry", "Copies your apps' settings into your home folder, as you. A "
                                                        "file it replaces is kept beside it as a numbered copy.");
    }
    // tar --diff exits 0 when every path matches, 1 when one differs or is
    // missing, 2 when it cannot compare (measured 2026-10-20). It counts an
    // owner or mode as a difference too, and a restore as this user, under
    // the user's umask, leaves both its own (measured 2026-10-29): those
    // alone are done.
    CheckResult check(const CheckContext &context) const override
    {
        if (geteuid() == 0)
            return {CheckState::CouldNotTell,
                    QCoreApplication::translate("gw::Carry", "Settings are copied as the user, never as root.")};
        QStringList argv{QStringLiteral("tar"), QStringLiteral("--diff"), QStringLiteral("--file"), m_file,
                         QStringLiteral("-C"), m_home};
        for (const SettingsRow &row : m_settings)
            argv << row.paths; // each starts with ".", so none reads as an option
        const CommandResult r = context.run(argv, kLongTimeoutMs);
        if (r.ok())
            return {CheckState::Done, {}};
        if (r.started && !r.timedOut && r.exitCode == 1)
            return {onlyOwnerOrModeDiffers(r) ? CheckState::Done : CheckState::NotDone, {}};
        return {CheckState::CouldNotTell,
                QCoreApplication::translate("gw::Carry", "The saved file could not be compared with your home folder.")};
    }
    // As the user, so nothing is written where the user could not write
    // already (INV-5).
    QList<Step> applySteps(const SystemIdentity &, const CheckContext &) const override
    {
        Manifest only;
        only.settings = m_settings;
        return {{QStringList{QStringLiteral("tar")} + extractArguments(m_file, m_home), false, Step::Tool::Generic,
                 QCoreApplication::translate("gw::Carry", "Copying your settings"), {}, createInput(only)}};
    }

private:
    QList<SettingsRow> m_settings;
    QString m_file, m_home;
};

// The firewall or printers, restored as root. Item checks never run as root,
// so what the file holds cannot be compared with what is on the system: the
// check reads not done, and restoring again keeps numbered copies (GRND-0073
// spec 4.6). It reads couldn't tell for a file holding anything but the names
// allowed, so no step runs (INV-4).
class SystemPartItem : public Item
{
public:
    SystemPartItem(SettingsRow part, QString file, QString unit) : m_part(std::move(part)), m_file(std::move(file)), m_unit(std::move(unit)) {}
    Level level() const override { return Level::NiceToHave; }
    CheckResult check(const CheckContext &context) const override
    {
        const ServiceState service = readService(context, m_unit);
        if (!service.known)
            return {CheckState::CouldNotTell, QCoreApplication::translate("gw::Carry", "The service manager did not answer.")};
        const SystemMembers members = systemMembers(context, m_file, m_part.paths.first());
        if (!members.problem.isEmpty())
            return {CheckState::CouldNotTell, members.problem};
        CheckResult result{CheckState::NotDone, {}};
        result.nothingToInstall = service.found;
        return result;
    }

protected:
    // tar's step that restores files, named on its standard input, into dir.
    // A replaced file is kept beside it as a numbered copy (INV-9).
    Step extractStep(const QStringList &files, const QString &dir, const QString &label) const
    {
        QByteArray input;
        for (const QString &file : files)
            input += file.toUtf8() + '\0'; // cppcheck-suppress useStlAlgorithm
        return {{QStringLiteral("tar"), QStringLiteral("--extract"), QStringLiteral("--file"), m_file, QStringLiteral("-C"), dir,
                 QStringLiteral("--strip-components=2"), QStringLiteral("--backup=numbered"), QStringLiteral("--no-same-owner"),
                 QStringLiteral("--no-overwrite-dir"), QStringLiteral("--verbatim-files-from"), QStringLiteral("--null"),
                 QStringLiteral("--files-from=-")},
                true, Step::Tool::Generic, label, {}, input};
    }
    // A file's own type comes from its folder, where SELinux is on (spec 15).
    static void relabel(const CheckContext &context, const QString &dir, const QString &label, QList<Step> *steps)
    {
        if (context.fileExists(QStringLiteral("/sys/fs/selinux/enforce")))
            *steps << Step{{QStringLiteral("restorecon"), QStringLiteral("-R"), dir}, true, Step::Tool::Generic, label};
    }

    SettingsRow m_part;
    QString m_file;
    QString m_unit;
};

class FirewallPartItem : public SystemPartItem
{
public:
    FirewallPartItem(const SettingsRow &part, const QString &file, bool hasFirewallItem)
        : SystemPartItem(part, file, QStringLiteral("firewalld")), m_hasFirewallItem(hasFirewallItem) {}
    QString id() const override { return kCarriedFirewallId; }
    QString title() const override { return QCoreApplication::translate("gw::Carry", "Firewall rules from your saved file"); }
    QString applySentence() const override
    {
        return QCoreApplication::translate("gw::Carry", "Changes system settings: puts back your firewall's zones and "
                                                        "services, and its default zone. A file it replaces is kept "
                                                        "beside it as a numbered copy.");
    }
    QStringList dependsOn() const override { return m_hasFirewallItem ? QStringList{QStringLiteral("firewall")} : QStringList(); }
    QList<Step> applySteps(const SystemIdentity &, const CheckContext &context) const override
    {
        const SystemMembers members = systemMembers(context, m_file, m_part.paths.first());
        if (!members.problem.isEmpty())
            return {};
        const QString label = QCoreApplication::translate("gw::Carry", "Putting your firewall rules back");
        const QString dir = QStringLiteral("/etc/firewalld");
        const QString conf = m_part.paths.first() + QStringLiteral("/firewalld.conf");
        // firewalld.conf holds settings a later version may read differently,
        // so only its default zone is the user's (spec 4.7, INV-9).
        QStringList files = members.files;
        files.removeAll(conf);
        QList<Step> steps{{{QStringLiteral("mkdir"), QStringLiteral("-p"), QStringLiteral("--"), dir + QStringLiteral("/zones"),
                            dir + QStringLiteral("/services"), dir + QStringLiteral("/policies"), dir + QStringLiteral("/ipsets")},
                           true, Step::Tool::Generic, label}};
        if (!files.isEmpty())
            steps << extractStep(files, dir, label);
        relabel(context, dir, label, &steps);
        steps << Step{{QStringLiteral("firewall-cmd"), QStringLiteral("--reload")}, true, Step::Tool::Generic, label};
        if (const QString zone = defaultZone(context, members.files.contains(conf) ? conf : QString()); !zone.isEmpty())
            steps << Step{{QStringLiteral("firewall-cmd"), QStringLiteral("--set-default-zone=") + zone}, true, Step::Tool::Generic, label};
        return steps;
    }

private:
    QString defaultZone(const CheckContext &context, const QString &member) const
    {
        if (member.isEmpty())
            return {};
        const CommandResult text = context.run({QStringLiteral("tar"), QStringLiteral("--extract"), QStringLiteral("--to-stdout"),
                                                QStringLiteral("--file"), m_file, member},
                                               kLongTimeoutMs, kManifestCap);
        if (!text.ok())
            return {};
        static const QRegularExpression zone(QStringLiteral(R"(\A[A-Za-z0-9_-]+\z)"));
        for (const QString &line : QString::fromUtf8(text.out).split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("DefaultZone="))) {
                const QString value = line.section(QLatin1Char('='), 1).trimmed();
                return zone.match(value).hasMatch() ? value : QString();
            }
        }
        return {};
    }

    bool m_hasFirewallItem;
};

class PrintersPartItem : public SystemPartItem
{
public:
    PrintersPartItem(const SettingsRow &part, const QString &file) : SystemPartItem(part, file, QStringLiteral("cups")) {}
    QString id() const override { return kCarriedPrintersId; }
    QString title() const override { return QCoreApplication::translate("gw::Carry", "Printers from your saved file"); }
    QString applySentence() const override
    {
        return QCoreApplication::translate("gw::Carry", "Changes system settings: puts back your printers, installing "
                                                        "CUPS if it is missing. A file it replaces is kept beside it as "
                                                        "a numbered copy.");
    }
    bool installsPackages() const override { return true; }
    QList<Step> applySteps(const SystemIdentity &, const CheckContext &context) const override
    {
        const SystemMembers members = systemMembers(context, m_file, m_part.paths.first());
        if (!members.problem.isEmpty())
            return {};
        const QString label = QCoreApplication::translate("gw::Carry", "Putting your printers back");
        const QString dir = QStringLiteral("/etc/cups");
        QList<Step> steps;
        if (!readService(context, m_unit).found)
            steps << Step{{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"), m_unit}, true,
                          Step::Tool::Zypper, QCoreApplication::translate("gw::Carry", "Installing CUPS")};
        // cupsd writes printers.conf when it stops, so it must not be running
        // while the file is replaced (spec 4.8, INV-10).
        steps << Step{{QStringLiteral("systemctl"), QStringLiteral("stop"), m_unit}, true, Step::Tool::Generic, label};
        steps << Step{{QStringLiteral("mkdir"), QStringLiteral("-p"), QStringLiteral("--"), dir + QStringLiteral("/ppd")}, true,
                      Step::Tool::Generic, label};
        if (!members.files.isEmpty())
            steps << extractStep(members.files, dir, label);
        for (const char *name : {"printers.conf", "classes.conf"}) {
            if (!members.files.contains(m_part.paths.first() + QLatin1Char('/') + QLatin1String(name)))
                continue;
            const QString path = dir + QLatin1Char('/') + QLatin1String(name);
            steps << Step{{QStringLiteral("chown"), QStringLiteral("root:lp"), path}, true, Step::Tool::Generic, label};
            steps << Step{{QStringLiteral("chmod"), QStringLiteral("600"), path}, true, Step::Tool::Generic, label};
        }
        relabel(context, dir, label, &steps);
        steps << Step{{QStringLiteral("systemctl"), QStringLiteral("enable"), QStringLiteral("--now"), m_unit}, true,
                      Step::Tool::Generic, label};
        return steps;
    }
};

// Kept drives, added to /etc/fstab as ExtraDrivesItem adds them, but never
// changing a drive's owner: these drives hold files (GRND-0073 spec 4.9).
class MountsItem : public Item
{
public:
    explicit MountsItem(QList<CarriedMount> mounts) : m_mounts(std::move(mounts)) {}
    QString id() const override { return kCarriedMountsId; }
    Level level() const override { return Level::NiceToHave; }
    QString title() const override { return QCoreApplication::translate("gw::Carry", "Drives from your saved file"); }
    QString applySentence() const override
    {
        return QCoreApplication::translate("gw::Carry", "Changes system settings: connects your drives at start-up, as "
                                                        "they were. A missing drive never stops the computer starting.");
    }
    CheckResult check(const CheckContext &context) const override
    {
        const std::optional<QByteArray> fstab = context.readFile(kFstabPath);
        if (!fstab)
            return {CheckState::CouldNotTell,
                    QCoreApplication::translate("gw::Carry", "The list of drives that connect at start-up could not be read.")};
        const Missing missing = missingMounts(context, *fstab);
        QStringList words;
        if (!missing.absent.isEmpty())
            words << QCoreApplication::translate("gw::Carry", "Not connected to this computer, so not added: %1.")
                         .arg(namesText(missing.absent));
        if (!missing.taken.isEmpty())
            words << QCoreApplication::translate("gw::Carry", "Already in the list of drives, so not added: %1.")
                         .arg(namesText(missing.taken));
        return {missing.add.isEmpty() ? CheckState::Done : CheckState::NotDone, words.join(QLatin1Char(' '))};
    }
    QList<Step> applySteps(const SystemIdentity &, const CheckContext &context) const override
    {
        const Missing missing = missingMounts(context, context.readFile(kFstabPath).value_or(QByteArray()));
        if (missing.add.isEmpty())
            return {};
        const QString label = QCoreApplication::translate("gw::Carry", "Connecting your drives at start-up");
        QStringList points;
        QByteArray lines = "\n# Written by Groundwork: drives from a saved setup.\n";
        for (const CarriedMount &mount : missing.add) {
            points << mount.point;
            // So a drive removed later cannot stop a start-up.
            QString options = mount.options;
            if (!options.split(QLatin1Char(',')).contains(QLatin1String("nofail")))
                options = options.isEmpty() ? QStringLiteral("nofail") : options + QStringLiteral(",nofail");
            lines += QStringLiteral("%1 %2 %3 %4 0 0\n").arg(mount.source, mount.point, mount.fstype, options).toUtf8();
        }
        QList<Step> steps{makeDirectoriesStep(points, label),
                          {{QStringLiteral("dd"), QStringLiteral("of=") + kFstabPath, QStringLiteral("oflag=append"),
                            QStringLiteral("conv=notrunc"), QStringLiteral("status=none")},
                           true, Step::Tool::Generic, label, {}, lines},
                          {{QStringLiteral("systemctl"), QStringLiteral("daemon-reload")}, true, Step::Tool::Generic, label}};
        for (const QString &point : std::as_const(points))
            steps << Step{{QStringLiteral("mount"), point}, true, Step::Tool::Generic, label};
        return steps;
    }

private:
    struct Missing
    {
        QList<CarriedMount> add;
        QStringList absent, taken;
    };
    // What to add now: a drive that is here, whose source and mount point
    // /etc/fstab does not have (INV-6).
    Missing missingMounts(const CheckContext &context, const QByteArray &fstab) const
    {
        QStringList sources, points;
        for (const QString &line : QString::fromUtf8(fstab).split(QLatin1Char('\n'))) {
            const QStringList fields = line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (fields.size() < 2 || fields[0].startsWith(QLatin1Char('#')))
                continue;
            sources << fields[0];
            points << fields[1];
        }
        Missing missing;
        for (const CarriedMount &mount : m_mounts) {
            QString device = mount.source;
            if (device.startsWith(QLatin1String("UUID=")))
                device = QStringLiteral("/dev/disk/by-uuid/") + device.mid(5);
            else if (device.startsWith(QLatin1String("LABEL=")))
                device = QStringLiteral("/dev/disk/by-label/") + device.mid(6);
            if (sources.contains(mount.source, Qt::CaseInsensitive) && points.contains(mount.point))
                continue; // this line, already there
            if (!context.fileExists(device))
                missing.absent << mount.point;
            else if (sources.contains(mount.source, Qt::CaseInsensitive) || points.contains(mount.point))
                missing.taken << mount.point;
            else
                missing.add << mount;
        }
        return missing;
    }

    QList<CarriedMount> m_mounts;
};

} // namespace

bool isCarriedId(const QString &id) { return id.startsWith(QLatin1String("carried-")); }

CarriedItems carriedItems(const Manifest &kept, const QString &file, const Catalogue &catalogue)
{
    CarriedItems carried;
    for (const QString &id : kept.items)
        (catalogue.find(id) ? carried.keptItems : carried.unknownItems) << id;
    for (const ZypperSource &source : kept.zypperSources)
        carried.owned.push_back(std::make_unique<SourceItem>(source));
    if (!kept.packages.isEmpty())
        carried.owned.push_back(std::make_unique<PackagesItem>(kept.packages));
    if (!kept.flatpaks.isEmpty()) {
        const bool fromFlathub = std::any_of(kept.flatpaks.cbegin(), kept.flatpaks.cend(),
                                             [](const FlatpakApp &app) { return app.remote == QLatin1String("flathub"); });
        carried.owned.push_back(
            std::make_unique<FlatpaksItem>(kept.flatpaks, fromFlathub && catalogue.find(QStringLiteral("flathub"))));
    }
    if (!kept.settings.isEmpty())
        carried.owned.push_back(std::make_unique<SettingsItem>(kept.settings, file, QDir::homePath()));
    for (const SettingsRow &part : kept.systemParts) {
        if (part.paths == QStringList{kFirewallPart})
            carried.owned.push_back(std::make_unique<FirewallPartItem>(part, file, bool(catalogue.find(QStringLiteral("firewall")))));
        else if (part.paths == QStringList{kPrintersPart})
            carried.owned.push_back(std::make_unique<PrintersPartItem>(part, file));
    }
    if (!kept.mounts.isEmpty())
        carried.owned.push_back(std::make_unique<MountsItem>(kept.mounts));
    return carried;
}

Catalogue withCarried(const Catalogue &catalogue, const CarriedItems &carried)
{
    QList<const Item *> items = catalogue.items();
    for (const std::unique_ptr<Item> &item : carried.owned)
        items << item.get();
    return Catalogue(items);
}

QString sourceStartsOffWhy(const ZypperSource &source, const QString &savedOn, const QString &thisSystem)
{
    if (!source.gpgcheck)
        return QCoreApplication::translate("gw::Carry", "Off: this source does not check its programs' signatures.");
    if (savedOn != thisSystem)
        return QCoreApplication::translate("gw::Carry", "Off: it was added on a different system, and may not suit "
                                                        "this one.");
    return {};
}

QByteArray repoFileText(const ZypperSource &source)
{
    // Every value passed the saved file's checks (its spec, 4.7), and a name
    // holds no control character, so none can add a line.
    QString text = QStringLiteral("[%1]\nname=%2\nbaseurl=%3\npriority=%4\nenabled=%5\nautorefresh=%6\ngpgcheck=%7\n")
                       .arg(source.alias, source.name, source.baseurl, QString::number(source.priority),
                            source.enabled ? QStringLiteral("1") : QStringLiteral("0"),
                            source.autorefresh ? QStringLiteral("1") : QStringLiteral("0"),
                            source.gpgcheck ? QStringLiteral("1") : QStringLiteral("0"));
    if (!source.gpgkey.isEmpty())
        text += QStringLiteral("gpgkey=") + source.gpgkey + QLatin1Char('\n');
    return text.toUtf8();
}

QStringList extractArguments(const QString &file, const QString &home)
{
    return {QStringLiteral("--extract"),      QStringLiteral("--file"),
            file,                             QStringLiteral("-C"),
            home,                             QStringLiteral("--backup=numbered"),
            QStringLiteral("--no-overwrite-dir"), QStringLiteral("--verbatim-files-from"),
            QStringLiteral("--null"),         QStringLiteral("--files-from=-")};
}

QString notInFile(const Manifest &chosen, const Manifest &file)
{
    auto lacks = [](const QString &name) {
        return QCoreApplication::translate("gw::Carry", "The saved file does not hold %1.").arg(isolated(name));
    };
    for (const QString &id : chosen.items)
        if (!file.items.contains(id)) // cppcheck-suppress useStlAlgorithm
            return lacks(id);
    for (const QString &name : chosen.packages)
        if (!file.packages.contains(name)) // cppcheck-suppress useStlAlgorithm
            return lacks(name);
    for (const FlatpakApp &app : chosen.flatpaks) {
        // The one change a choice may make: a Flathub app saved for the
        // user restored for everyone (INV-11).
        const FlatpakApp asSaved{app.app, app.remote, QStringLiteral("user")};
        const bool movedToEveryone = app.remote == QLatin1String("flathub")
                                     && app.installation == QLatin1String("system") && file.flatpaks.contains(asSaved);
        if (!file.flatpaks.contains(app) && !movedToEveryone)
            return lacks(app.app);
    }
    for (const ZypperSource &source : chosen.zypperSources)
        if (!file.zypperSources.contains(source)) // cppcheck-suppress useStlAlgorithm
            return lacks(source.alias);
    for (const FlatpakRemote &remote : chosen.flatpakRemotes)
        if (!file.flatpakRemotes.contains(remote)) // cppcheck-suppress useStlAlgorithm
            return lacks(remote.name);
    for (const SettingsRow &row : chosen.settings)
        if (!file.settings.contains(row)) // cppcheck-suppress useStlAlgorithm
            return lacks(row.name);
    for (const SettingsRow &row : chosen.systemParts)
        if (!file.systemParts.contains(row)) // cppcheck-suppress useStlAlgorithm
            return lacks(row.name);
    for (const CarriedMount &mount : chosen.mounts)
        if (!file.mounts.contains(mount)) // cppcheck-suppress useStlAlgorithm
            return lacks(mount.point);
    for (const CarriedShare &share : chosen.shares)
        if (!file.shares.contains(share)) // cppcheck-suppress useStlAlgorithm
            return lacks(share.path);
    return {};
}

} // namespace gw
