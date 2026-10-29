#include "carry.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>

#include <algorithm>

namespace gw {
namespace {

constexpr int kLongTimeoutMs = 10 * 60 * 1000; // du over a home, tar over a large file

bool hasControl(const QString &text)
{
    return std::any_of(text.cbegin(), text.cend(), [](QChar c) { return c.unicode() < 0x20 || c.unicode() == 0x7f; });
}

// The installer chooses the kernel, boot loader and security framework for
// the computer it installs, and a carried one can clash with its choice:
// kernel-default stopped every program installing beside
// kernel-default-base (GRND-0086).
bool isInstallerChosen(const QString &name)
{
    static const QRegularExpression chosen(
        QStringLiteral("^(kernel-(default|longterm|preempt|rt|vanilla|kvmsmall|64kb)(-base|-extra|-optional)?"
                       "|grub2(-.+)?|shim|systemd-boot|sdbootutil(-.+)?|patterns-base-(selinux|apparmor))$"));
    return chosen.match(name).hasMatch();
}

// The distribution's own trees, which a fresh install adds (spec 4.3).
const QStringList kDistributionTrees{
    QStringLiteral("download.opensuse.org/tumbleweed/"),
    QStringLiteral("download.opensuse.org/update/tumbleweed/"),
    QStringLiteral("download.opensuse.org/slowroll/"),
    QStringLiteral("download.opensuse.org/update/slowroll/"),
    QStringLiteral("download.opensuse.org/distribution/leap/"),
    QStringLiteral("download.opensuse.org/update/leap/"),
    QStringLiteral("download.opensuse.org/debug/"),
    QStringLiteral("download.opensuse.org/source/"),
    QStringLiteral("codecs.opensuse.org/"),
};

// The only folders of ~/.local/share offered (spec 4.4).
const QStringList kSharedSettings{
    QStringLiteral("applications"), QStringLiteral("color-schemes"), QStringLiteral("konsole"),
    QStringLiteral("kxmlgui5"),     QStringLiteral("plasma"),        QStringLiteral("fonts"),
};

bool isInstallation(const QString &text) { return text == QLatin1String("system") || text == QLatin1String("user"); }

bool truthy(const QString &value) { return value == QLatin1String("1") || value == QLatin1String("yes") || value == QLatin1String("true"); }

// A member is under a setting's path when it is that path, or inside it.
bool isUnder(const QString &member, const QString &path)
{
    return member == path || member.startsWith(path + QLatin1Char('/'));
}

QString strippedMember(const QString &member)
{
    QString path = member;
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return path;
}

QString skippedName(const QString &value)
{
    return QCoreApplication::translate("gw::Carry", "Skipped “%1”: it is not a name Groundwork can use.").arg(value);
}

QStringList strings(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &v : value.toArray())
        out << v.toString();
    return out;
}

} // namespace

bool isPackageName(const QString &text)
{
    static const QRegularExpression name(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9+._-]*$"));
    return name.match(text).hasMatch();
}

bool isFlatpakAppId(const QString &text)
{
    static const QRegularExpression id(QStringLiteral("^[A-Za-z_][A-Za-z0-9_-]*(?:\\.[A-Za-z_][A-Za-z0-9_-]*){2,}$"));
    return id.match(text).hasMatch();
}

bool isAlias(const QString &text)
{
    static const QRegularExpression alias(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9:._+-]*$"));
    return alias.match(text).hasMatch();
}

bool isSourceUrl(const QString &text)
{
    const bool scheme = text.startsWith(QLatin1String("https://")) || text.startsWith(QLatin1String("http://"))
                        || text.startsWith(QLatin1String("ftp://"));
    return scheme && !text.contains(QLatin1Char(' ')) && !hasControl(text);
}

bool isSettingsPath(const QString &path)
{
    if (!path.startsWith(QLatin1Char('.')) || hasControl(path))
        return false;
    const QStringList parts = path.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
            return false;
    }
    return !(parts.size() == 1
             && (path == QLatin1String(".cache") || path == QLatin1String(".local") || path == QLatin1String(".var")));
}

QStringList gatherItems(const Catalogue &catalogue, const CheckResults &results)
{
    QStringList ids;
    for (const Item *item : catalogue.items()) {
        if (!item->isPreparation() && results.value(item->id()).state == CheckState::Done)
            ids << item->id();
    }
    return ids;
}

QStringList askedForPackages(const QByteArray &zypperOut)
{
    QStringList names;
    for (const QString &line : QString::fromUtf8(zypperOut).split(QLatin1Char('\n'))) {
        const QStringList columns = line.split(QLatin1Char('|'));
        const QString name = columns.value(1).trimmed();
        if (columns[0].trimmed() == QLatin1String("i+") && isPackageName(name) && !isInstallerChosen(name))
            names << name;
    }
    return names;
}

QList<FlatpakApp> parseFlatpakApps(const QByteArray &flatpakOut)
{
    QList<FlatpakApp> apps;
    for (const QString &line : QString::fromUtf8(flatpakOut).split(QLatin1Char('\n'))) {
        const QStringList columns = line.split(QLatin1Char('\t'));
        if (columns.size() >= 3 && isFlatpakAppId(columns[0]) && isAlias(columns[1]) && isInstallation(columns[2]))
            apps << FlatpakApp{columns[0], columns[1], columns[2]};
    }
    return apps;
}

QList<FlatpakRemote> parseFlatpakRemotes(const QByteArray &flatpakOut)
{
    QList<FlatpakRemote> remotes;
    for (const QString &line : QString::fromUtf8(flatpakOut).split(QLatin1Char('\n'))) {
        const QStringList columns = line.split(QLatin1Char('\t'));
        if (columns.size() < 3 || columns[0] == QLatin1String("flathub"))
            continue;
        // Options lead with the installation: "user,no-enumerate" (measured 2026-10-19).
        const QString installation = columns[2].section(QLatin1Char(','), 0, 0);
        if (isAlias(columns[0]) && isSourceUrl(columns[1]) && isInstallation(installation))
            remotes << FlatpakRemote{columns[0], columns[1], installation};
    }
    return remotes;
}

std::optional<QList<ZypperSource>> parseRepoFile(const QByteArray &text)
{
    QList<ZypperSource> sources;
    for (const QString &raw : QString::fromUtf8(text).split(QLatin1Char('\n'))) {
        // A line starting with a space continues a value; only the first is kept.
        if (raw.startsWith(QLatin1Char(' ')) || raw.startsWith(QLatin1Char('\t')))
            continue;
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            ZypperSource source;
            source.alias = line.mid(1, line.size() - 2).trimmed();
            sources << source;
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0 || sources.isEmpty())
            return std::nullopt;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        ZypperSource &source = sources.last();
        if (key == QLatin1String("name"))
            source.name = value;
        else if (key == QLatin1String("baseurl") && source.baseurl.isEmpty())
            source.baseurl = value;
        else if (key == QLatin1String("gpgkey") && source.gpgkey.isEmpty())
            source.gpgkey = value;
        else if (key == QLatin1String("priority"))
            source.priority = value.toInt();
        else if (key == QLatin1String("enabled"))
            source.enabled = truthy(value);
        else if (key == QLatin1String("autorefresh"))
            source.autorefresh = truthy(value);
        else if (key == QLatin1String("gpgcheck"))
            source.gpgcheck = truthy(value);
    }
    if (sources.isEmpty())
        return std::nullopt;
    for (ZypperSource &source : sources) {
        if (!isAlias(source.alias) || source.baseurl.isEmpty())
            return std::nullopt;
        if (source.name.isEmpty())
            source.name = source.alias;
    }
    return sources;
}

bool isDistributionSource(const QString &baseurl)
{
    const qsizetype scheme = baseurl.indexOf(QLatin1String("://"));
    const QString rest = scheme >= 0 ? baseurl.mid(scheme + 3) : baseurl;
    return std::any_of(kDistributionTrees.cbegin(), kDistributionTrees.cend(),
                       [&](const QString &tree) { return rest.startsWith(tree); });
}

void gatherPrograms(const CheckContext &context, Gathered *into)
{
    const CommandResult zypper = context.run({QStringLiteral("zypper"), QStringLiteral("--no-refresh"),
                                              QStringLiteral("search"), QStringLiteral("--installed-only"),
                                              QStringLiteral("-t"), QStringLiteral("package")},
                                             kLongTimeoutMs);
    if (!zypper.started)
        into->packagesProblem = QCoreApplication::translate("gw::Carry", "zypper could not be run.");
    else if (zypper.exitCode == 7) // ZYPPER_EXIT_ZYPP_LOCKED
        into->packagesProblem = QCoreApplication::translate(
            "gw::Carry", "Another program is using zypper. Close it, then go back and try again.");
    else if (!zypper.ok())
        into->packagesProblem = QCoreApplication::translate("gw::Carry", "zypper could not list the installed "
                                                                         "programs (exit code %1).")
                                    .arg(zypper.exitCode);
    else
        into->packages = askedForPackages(zypper.out);

    const CommandResult flatpak = context.run({QStringLiteral("flatpak"), QStringLiteral("list"), QStringLiteral("--app"),
                                               QStringLiteral("--columns=application,origin,installation")});
    if (!flatpak.started)
        into->flatpaksProblem = QCoreApplication::translate("gw::Carry", "Flatpak is not installed.");
    else if (!flatpak.ok())
        into->flatpaksProblem = QCoreApplication::translate("gw::Carry", "Flatpak could not list its apps.");
    else
        into->flatpaks = parseFlatpakApps(flatpak.out);
}

void gatherSources(const CheckContext &context, Gathered *into)
{
    const QString dir = QStringLiteral("/etc/zypp/repos.d/");
    for (const QString &name : context.entries(dir)) {
        if (!name.endsWith(QLatin1String(".repo")))
            continue;
        const std::optional<QByteArray> text = context.readFile(dir + name);
        const std::optional<QList<ZypperSource>> sources = text ? parseRepoFile(*text) : std::nullopt;
        if (!sources) {
            into->unreadableRepos << name;
            continue;
        }
        // A source a new install could not reach, such as a local folder, is not carried.
        for (const ZypperSource &source : *sources) {
            if (!isDistributionSource(source.baseurl) && isSourceUrl(source.baseurl))
                into->zypperSources << source;
        }
    }

    const CommandResult remotes = context.run(
        {QStringLiteral("flatpak"), QStringLiteral("remotes"), QStringLiteral("--columns=name,url,options")});
    if (!remotes.started)
        into->remotesProblem = QCoreApplication::translate("gw::Carry", "Flatpak is not installed.");
    else if (!remotes.ok())
        into->remotesProblem = QCoreApplication::translate("gw::Carry", "Flatpak could not list its sources.");
    else
        into->flatpakRemotes = parseFlatpakRemotes(remotes.out);
}

QList<SettingsRow> gatherSettings(const CheckContext &context, const QString &home)
{
    QStringList entries;
    for (const QString &name : context.entries(home + QStringLiteral("/.config"), true))
        entries << QStringLiteral(".config/") + name;
    for (const QString &name : context.entries(home, true)) {
        if (name.startsWith(QLatin1Char('.')) && name != QLatin1String(".cache") && name != QLatin1String(".config")
            && name != QLatin1String(".local") && name != QLatin1String(".var"))
            entries << name;
    }
    for (const QString &id : context.entries(home + QStringLiteral("/.var/app"))) {
        const QString config = QStringLiteral(".var/app/") + id + QStringLiteral("/config");
        if (context.fileExists(home + QLatin1Char('/') + config))
            entries << config;
    }
    for (const QString &name : kSharedSettings) {
        const QString path = QStringLiteral(".local/share/") + name;
        if (context.fileExists(home + QLatin1Char('/') + path))
            entries << path;
    }
    entries.removeIf([](const QString &entry) { return !isSettingsPath(entry); });

    QList<SettingsRow> rows = groupSettings(entries);
    if (rows.isEmpty())
        return rows;
    // One du for every path. It exits 1 when part of a folder is
    // unreadable, and still prints the rest.
    QStringList argv{QStringLiteral("du"), QStringLiteral("-s"), QStringLiteral("-b"), QStringLiteral("--")};
    for (const QString &entry : std::as_const(entries))
        argv << home + QLatin1Char('/') + entry;
    const CommandResult du = context.run(argv, kLongTimeoutMs);
    QHash<QString, qint64> sizes;
    // Any other end, du missing or killed say, gives no size at all.
    if (du.started && !du.timedOut && (du.exitCode == 0 || du.exitCode == 1)) {
        for (const QString &line : QString::fromUtf8(du.out).split(QLatin1Char('\n'))) {
            const qsizetype tab = line.indexOf(QLatin1Char('\t'));
            bool ok = false;
            const qint64 size = tab > 0 ? line.left(tab).toLongLong(&ok) : 0;
            if (ok)
                sizes.insert(line.mid(tab + 1), size);
        }
    }
    for (SettingsRow &row : rows) {
        for (const QString &path : std::as_const(row.paths)) {
            const auto size = sizes.constFind(home + QLatin1Char('/') + path);
            if (size == sizes.cend()) {
                row.bytes = -1;
                break;
            }
            row.bytes += *size;
        }
    }
    return rows;
}

bool startsOn(const SettingsRow &row) { return !row.secret && row.bytes >= 0 && row.bytes <= kLargeSettings; }

QString saveProblem(qint64 bytes, qint64 freeBytes, const QString &fstype)
{
    const QLocale locale;
    if (bytes < 0)
        return QCoreApplication::translate("gw::Carry", "Groundwork could not tell the size of some settings "
                                                        "switched on. Switch them off to save.");
    if (freeBytes < 0)
        return QCoreApplication::translate("gw::Carry", "Groundwork could not tell how much room this folder has. "
                                                        "Choose another folder.");
    if (fstype == QLatin1String("vfat") && bytes > kVfatLimit)
        return QCoreApplication::translate("gw::Carry", "This drive uses FAT, which cannot hold a file bigger than "
                                                        "4 GiB, and the settings switched on need %1. Switch some "
                                                        "off, or choose a folder on another drive.")
            .arg(locale.formattedDataSize(bytes, 1, QLocale::DataSizeIecFormat));
    if (bytes > freeBytes)
        return QCoreApplication::translate("gw::Carry", "There is not enough room here: the settings switched on "
                                                        "need %1, and %2 is free. Switch some off, or choose "
                                                        "another folder.")
            .arg(locale.formattedDataSize(bytes, 1, QLocale::DataSizeIecFormat),
                 locale.formattedDataSize(freeBytes, 1, QLocale::DataSizeIecFormat));
    return {};
}

FolderSpace readFolderSpace(const CheckContext &context, const QString &dir)
{
    FolderSpace space;
    const CommandResult df = context.run(
        {QStringLiteral("df"), QStringLiteral("--output=avail"), QStringLiteral("-B1"), QStringLiteral("--"), dir});
    const QStringList lines = QString::fromUtf8(df.out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    bool ok = false;
    const qint64 avail = lines.size() >= 2 ? lines[1].trimmed().toLongLong(&ok) : 0;
    if (df.ok() && ok)
        space.freeBytes = avail;
    const CommandResult findmnt = context.run(
        {QStringLiteral("findmnt"), QStringLiteral("-no"), QStringLiteral("FSTYPE"), QStringLiteral("--target"), dir});
    if (findmnt.ok())
        space.fstype = QString::fromUtf8(findmnt.out).trimmed();
    return space;
}

QString setupFileName(const QString &hostname, const QDate &date)
{
    QString name;
    for (QChar c : hostname) {
        if (c.isLetterOrNumber() && c.unicode() < 0x80)
            name += c;
        else if (c == QLatin1Char('-') || c == QLatin1Char('.') || c == QLatin1Char('_'))
            name += c;
    }
    if (name.isEmpty())
        name = QStringLiteral("computer");
    return QStringLiteral("groundwork-setup-%1-%2.tar").arg(name, date.toString(Qt::ISODate));
}

QJsonObject manifestJson(const Manifest &manifest)
{
    QJsonArray flatpaks;
    for (const FlatpakApp &a : manifest.flatpaks)
        flatpaks.append(QJsonObject{{"app", a.app}, {"remote", a.remote}, {"installation", a.installation}});
    QJsonArray sources;
    for (const ZypperSource &s : manifest.zypperSources)
        sources.append(QJsonObject{{"kind", "zypper"},
                                   {"alias", s.alias},
                                   {"name", s.name},
                                   {"baseurl", s.baseurl},
                                   {"priority", s.priority},
                                   {"enabled", s.enabled},
                                   {"autorefresh", s.autorefresh},
                                   {"gpgcheck", s.gpgcheck},
                                   {"gpgkey", s.gpgkey.isEmpty() ? QJsonValue() : QJsonValue(s.gpgkey)}});
    for (const FlatpakRemote &r : manifest.flatpakRemotes)
        sources.append(QJsonObject{{"kind", "flatpak"}, {"name", r.name}, {"url", r.url}, {"installation", r.installation}});
    QJsonArray settings;
    for (const SettingsRow &row : manifest.settings)
        settings.append(QJsonObject{{"name", row.name},
                                    {"paths", QJsonArray::fromStringList(row.paths)},
                                    {"secret", row.secret},
                                    {"bytes", double(row.bytes)}});
    return {{"format", manifest.format},
            {"created", manifest.created},
            {"groundwork", manifest.groundwork},
            {"system", QJsonObject{{"id", manifest.systemId}, {"version_id", manifest.systemVersion}}},
            {"items", QJsonArray::fromStringList(manifest.items)},
            {"packages", QJsonArray::fromStringList(manifest.packages)},
            {"flatpaks", flatpaks},
            {"sources", sources},
            {"settings", settings}};
}

std::optional<Manifest> readManifest(const QJsonObject &json, QString *why, QStringList *skipped)
{
    const QJsonValue format = json.value("format");
    if (!format.isDouble() || format.toInt() < 1) {
        *why = QCoreApplication::translate("gw::Carry", "This is not a Groundwork setup file.");
        return std::nullopt;
    }
    if (format.toInt() > kSetupFormat) {
        *why = QCoreApplication::translate("gw::Carry", "This file was saved by a newer Groundwork. Update "
                                                        "Groundwork to read it.");
        return std::nullopt;
    }
    Manifest m;
    m.format = format.toInt();
    m.created = json.value("created").toString();
    m.groundwork = json.value("groundwork").toString();
    m.systemId = json.value("system").toObject().value("id").toString();
    m.systemVersion = json.value("system").toObject().value("version_id").toString();

    // Unknown keys are ignored, so a later format-1 writer may add fields.
    for (const QString &id : strings(json.value("items"))) {
        if (isPackageName(id))
            m.items << id;
        else
            *skipped << skippedName(id);
    }
    for (const QString &name : strings(json.value("packages"))) {
        if (isInstallerChosen(name))
            continue; // a file saved before GRND-0086 may hold one
        if (isPackageName(name))
            m.packages << name;
        else
            *skipped << skippedName(name);
    }
    for (const QJsonValue &v : json.value("flatpaks").toArray()) {
        const QJsonObject o = v.toObject();
        const FlatpakApp app{o.value("app").toString(), o.value("remote").toString(), o.value("installation").toString()};
        if (isFlatpakAppId(app.app) && isAlias(app.remote) && isInstallation(app.installation))
            m.flatpaks << app;
        else
            *skipped << skippedName(app.app);
    }
    for (const QJsonValue &v : json.value("sources").toArray()) {
        const QJsonObject o = v.toObject();
        const QString kind = o.value("kind").toString();
        if (kind == QLatin1String("zypper")) {
            ZypperSource s;
            s.alias = o.value("alias").toString();
            s.name = o.value("name").toString(s.alias);
            s.baseurl = o.value("baseurl").toString();
            s.gpgkey = o.value("gpgkey").toString();
            s.priority = o.value("priority").toInt(99);
            s.enabled = o.value("enabled").toBool(true);
            s.autorefresh = o.value("autorefresh").toBool(false);
            s.gpgcheck = o.value("gpgcheck").toBool(true);
            if (isAlias(s.alias) && !s.name.isEmpty() && !hasControl(s.name) && isSourceUrl(s.baseurl)
                && (s.gpgkey.isEmpty() || isSourceUrl(s.gpgkey)) && s.priority >= 1 && s.priority <= 200)
                m.zypperSources << s;
            else
                *skipped << skippedName(s.alias);
        } else if (kind == QLatin1String("flatpak")) {
            const FlatpakRemote r{o.value("name").toString(), o.value("url").toString(),
                                  o.value("installation").toString()};
            if (isAlias(r.name) && isSourceUrl(r.url) && isInstallation(r.installation))
                m.flatpakRemotes << r;
            else
                *skipped << skippedName(r.name);
        } else {
            *skipped << skippedName(kind);
        }
    }
    for (const QJsonValue &v : json.value("settings").toArray()) {
        const QJsonObject o = v.toObject();
        SettingsRow row{o.value("name").toString(), strings(o.value("paths")), o.value("secret").toBool(),
                        qint64(o.value("bytes").toDouble())};
        // A path outside the home folder refuses the whole file (INV-3).
        for (const QString &path : std::as_const(row.paths)) {
            if (!isSettingsPath(path)) {
                *why = QCoreApplication::translate("gw::Carry", "The file names a setting outside the home "
                                                                "folder: %1")
                           .arg(path);
                return std::nullopt;
            }
        }
        if (row.name.isEmpty() || row.paths.isEmpty())
            *skipped << skippedName(row.name);
        else
            m.settings << row;
    }
    return m;
}

std::optional<Manifest> readManifest(const QByteArray &json, QString *why, QStringList *skipped)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        *why = QCoreApplication::translate("gw::Carry", "The setup list in the file is damaged.");
        return std::nullopt;
    }
    return readManifest(document.object(), why, skipped);
}

QString membersProblem(const QStringList &members, const Manifest &manifest)
{
    const qsizetype lists = members.count(kManifestName);
    if (lists == 0)
        return QCoreApplication::translate("gw::Carry", "The file holds no setup list.");
    if (lists > 1)
        return QCoreApplication::translate("gw::Carry", "The file holds more than one setup list.");
    QStringList paths;
    for (const SettingsRow &row : manifest.settings)
        paths << row.paths;
    for (const QString &member : members) {
        if (member == kManifestName)
            continue;
        const QString path = strippedMember(member);
        if (!isSettingsPath(path))
            return QCoreApplication::translate("gw::Carry", "The file holds %1, which is outside the home folder.")
                .arg(member);
        if (std::none_of(paths.cbegin(), paths.cend(), [&](const QString &p) { return isUnder(path, p); }))
            return QCoreApplication::translate("gw::Carry", "The file holds %1, which its setup list does not name.")
                .arg(member);
    }
    for (const QString &p : std::as_const(paths)) {
        if (std::none_of(members.cbegin(), members.cend(),
                         [&](const QString &member) { return isUnder(strippedMember(member), p); }))
            return QCoreApplication::translate("gw::Carry", "The file is missing %1.").arg(p);
    }
    return {};
}

QStringList createArguments(const QString &partial, const QString &stateDir, const QString &home)
{
    // An unreadable file is left out with a warning, not a failure (GRND-0085).
    return {QStringLiteral("--create"), QStringLiteral("--format=posix"), QStringLiteral("--ignore-failed-read"),
            QStringLiteral("--file"), partial, QStringLiteral("-C"), stateDir, kManifestName, QStringLiteral("-C"), home,
            QStringLiteral("--verbatim-files-from"), QStringLiteral("--null"), QStringLiteral("--files-from=-")};
}

QStringList unreadableSettings(const Manifest &manifest, const QString &home)
{
    // As tar reads them: a link is stored, not followed, and a folder must
    // be both read and entered.
    auto unreadable = [](const QFileInfo &info) {
        if (info.isSymLink())
            return false;
        return !info.isReadable() || (info.isDir() && !info.isExecutable());
    };
    auto anyUnreadable = [&](const QString &path) {
        const QFileInfo top(home + QLatin1Char('/') + path);
        if (unreadable(top))
            return true;
        if (!top.isDir() || top.isSymLink())
            return false;
        QDirIterator it(top.filePath(), QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (unreadable(it.nextFileInfo()))
                return true;
        }
        return false;
    };
    QStringList names;
    for (const SettingsRow &row : manifest.settings) {
        if (std::any_of(row.paths.cbegin(), row.paths.cend(), anyUnreadable))
            names << row.name;
    }
    return names;
}

Manifest withoutUnreadablePaths(const Manifest &manifest, const QString &home)
{
    // A folder that can be read but not entered still gets its own member.
    auto unreadable = [&](const QString &path) {
        const QFileInfo info(home + QLatin1Char('/') + path);
        return !info.isSymLink() && !info.isReadable();
    };
    Manifest kept = manifest;
    for (SettingsRow &row : kept.settings)
        row.paths.removeIf(unreadable);
    kept.settings.removeIf([](const SettingsRow &row) { return row.paths.isEmpty(); });
    return kept;
}

QByteArray createInput(const Manifest &manifest)
{
    QByteArray input;
    for (const SettingsRow &row : manifest.settings) {
        for (const QString &path : row.paths)
            input += path.toUtf8() + '\0';
    }
    return input;
}

// Literal names, so a name is listed as stored; a line break inside one
// splits it into names no setting lists, which refuses the file.
QStringList listArguments(const QString &file)
{
    return {QStringLiteral("--list"), QStringLiteral("--quoting-style=literal"), QStringLiteral("--file"), file};
}

std::optional<Manifest> readSetupFile(const CheckContext &context, const QString &file, QString *why,
                                      QStringList *skipped)
{
    const CommandResult list = context.run(QStringList{QStringLiteral("tar")} + listArguments(file), kLongTimeoutMs);
    if (!list.ok()) {
        *why = QCoreApplication::translate("gw::Carry", "The file could not be read as a Groundwork setup file.");
        return std::nullopt;
    }
    const QStringList members = QString::fromUtf8(list.out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    // The manifest once, before reading it.
    if (members.count(kManifestName) != 1) {
        *why = membersProblem(members, {});
        return std::nullopt;
    }
    const CommandResult text = context.run({QStringLiteral("tar"), QStringLiteral("--extract"),
                                            QStringLiteral("--to-stdout"), QStringLiteral("--file"), file,
                                            kManifestName},
                                           kLongTimeoutMs);
    if (!text.ok()) {
        *why = QCoreApplication::translate("gw::Carry", "The file could not be read as a Groundwork setup file.");
        return std::nullopt;
    }
    std::optional<Manifest> manifest = readManifest(text.out, why, skipped);
    if (!manifest)
        return std::nullopt;
    if (const QString problem = membersProblem(members, *manifest); !problem.isEmpty()) {
        *why = problem;
        return std::nullopt;
    }
    return manifest;
}

QStringList secretMembers(const QStringList &members, const Manifest &manifest)
{
    QStringList secret;
    for (const QString &member : members) {
        for (const SettingsRow &row : manifest.settings) {
            // As tar --delete names it: a folder without its trailing slash,
            // and nothing inside one already named, which it deletes too.
            const QString path = strippedMember(member);
            if (std::any_of(secret.cbegin(), secret.cend(), [&](const QString &s) { return isUnder(path, s); }))
                break;
            if (row.secret
                && std::any_of(row.paths.cbegin(), row.paths.cend(), [&](const QString &p) { return isUnder(path, p); })) {
                secret << path;
                break;
            }
        }
    }
    return secret;
}

Manifest withoutSecrets(const Manifest &manifest)
{
    Manifest kept = manifest;
    kept.settings.removeIf([](const SettingsRow &row) { return row.secret; });
    return kept;
}

} // namespace gw
