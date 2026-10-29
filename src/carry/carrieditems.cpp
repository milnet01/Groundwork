#include "carrieditems.h"

#include "core/translations.h"

#include <QCoreApplication>
#include <QDir>

#include <algorithm>
#include <utility>

#include <unistd.h>

namespace gw {
namespace {

constexpr int kLongTimeoutMs = 10 * 60 * 1000; // rpm over hundreds of names, tar over a large file
const QString kReposDir = QStringLiteral("/etc/zypp/repos.d");
constexpr qsizetype kNamesShown = 10;

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
    for (const QString &name : all.mid(0, kNamesShown))
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
        for (const QString &name : context.entries(kReposDir)) {
            if (!name.endsWith(QLatin1String(".repo")))
                continue;
            const std::optional<QByteArray> text = context.readFile(kReposDir + QLatin1Char('/') + name);
            const std::optional<QList<ZypperSource>> sources = text ? parseRepoFile(*text) : std::nullopt;
            if (!sources)
                unreadable << name;
            taken = taken || name == fileName;
            for (const ZypperSource &s : sources.value_or(QList<ZypperSource>())) {
                if (s.alias == m_source.alias || s.baseurl == m_source.baseurl)
                    return {CheckState::Done, {}};
            }
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
    // The kept apps not installed; nothing when Flatpak could not list them.
    // Flatpak not installed at all means none is.
    std::optional<QList<FlatpakApp>> absentApps(const CheckContext &context, QStringList *names) const
    {
        const CommandResult r = context.run({QStringLiteral("flatpak"), QStringLiteral("list"), QStringLiteral("--app"),
                                             QStringLiteral("--columns=application,installation")});
        if (r.started && !r.ok())
            return std::nullopt;
        const QStringList lines = QString::fromUtf8(r.out).split(QLatin1Char('\n'));
        QList<FlatpakApp> absent;
        for (const FlatpakApp &app : m_apps) {
            if (!lines.contains(app.app + QLatin1Char('\t') + app.installation)) {
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
        if (!file.items.contains(id))
            return lacks(id);
    for (const QString &name : chosen.packages)
        if (!file.packages.contains(name))
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
        if (!file.zypperSources.contains(source))
            return lacks(source.alias);
    for (const FlatpakRemote &remote : chosen.flatpakRemotes)
        if (!file.flatpakRemotes.contains(remote))
            return lacks(remote.name);
    for (const SettingsRow &row : chosen.settings)
        if (!file.settings.contains(row))
            return lacks(row.name);
    return {};
}

} // namespace gw
