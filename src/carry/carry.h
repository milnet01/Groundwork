// Carry (GRND-0078; docs/specs/GRND-0078-saved-setup-file.md): gathering a
// setup, and writing and reading the saved file's manifest. Like Items, it
// names commands and runs nothing itself (docs/design.md, What may depend
// on what).
#pragma once

#include "core/selection.h"

#include <QByteArray>
#include <QDate>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace gw {

// The manifest's own name, in the state folder and in the archive (spec 4.6).
inline const QString kManifestName = QStringLiteral("groundwork-setup.json");
constexpr int kSetupFormat = 1;
// A settings row bigger than this starts switched off (spec 4.4).
constexpr qint64 kLargeSettings = qint64(100) << 20;
// The largest file vfat holds (spec 4.5, INV-9).
constexpr qint64 kVfatLimit = (qint64(4) << 30) - 1;

struct FlatpakApp
{
    QString app, remote, installation; // installation: "system" or "user"
    bool operator==(const FlatpakApp &) const = default;
};

struct ZypperSource
{
    QString alias, name, baseurl, gpgkey; // gpgkey empty: none
    int priority = 99;
    bool enabled = true, autorefresh = false, gpgcheck = true;
    bool operator==(const ZypperSource &) const = default;
};

struct FlatpakRemote
{
    QString name, url, installation;
    bool operator==(const FlatpakRemote &) const = default;
};

struct SettingsRow
{
    QString name;
    QStringList paths; // relative to the home folder
    bool secret = false;
    qint64 bytes = 0;
    bool operator==(const SettingsRow &) const = default;
};

struct Manifest
{
    int format = kSetupFormat;
    QString created, groundwork, systemId, systemVersion;
    QStringList items, packages;
    QList<FlatpakApp> flatpaks;
    QList<ZypperSource> zypperSources;
    QList<FlatpakRemote> flatpakRemotes;
    QList<SettingsRow> settings;
    bool operator==(const Manifest &) const = default;
};

// § 4.7's patterns (INV-10): a package name or item id; a Flatpak app id;
// a remote's name or a source's alias; a URL.
bool isPackageName(const QString &text);
bool isFlatpakAppId(const QString &text);
bool isAlias(const QString &text);
bool isSourceUrl(const QString &text);

// INV-3: relative, no "..", starts with ".", and not .cache, .local or
// .var alone.
bool isSettingsPath(const QString &path);

// Groundwork's done items that are not preparations (spec 4.1, INV-8).
QStringList gatherItems(const Catalogue &catalogue, const CheckResults &results);

// The names of the i+ rows of zypper search's table (spec 4.2, INV-7).
QStringList askedForPackages(const QByteArray &zypperOut);
QList<FlatpakApp> parseFlatpakApps(const QByteArray &flatpakOut);
// Every remote but flathub, with a URL the reader would take.
QList<FlatpakRemote> parseFlatpakRemotes(const QByteArray &flatpakOut);
// The sources one .repo file holds; nothing when it cannot be parsed.
std::optional<QList<ZypperSource>> parseRepoFile(const QByteArray &text);
// True for one of the distribution's own trees, which a fresh install adds
// (spec 4.3, INV-6).
bool isDistributionSource(const QString &baseurl);

// What the carry job's first page shows, gathered without root. A problem
// is plain words for a group that "couldn't tell".
struct Gathered
{
    QStringList packages;
    QString packagesProblem;
    QList<FlatpakApp> flatpaks;
    QString flatpaksProblem;
    QList<ZypperSource> zypperSources;
    QStringList unreadableRepos; // file names
    QList<FlatpakRemote> flatpakRemotes;
    QString remotesProblem;
    QList<SettingsRow> settings;
};

// The candidate entries (spec 4.4), relative to the home folder, as named
// rows: an app's entries together, secrets marked, sizes not yet read.
// The table is settingsgroups.cpp.
QList<SettingsRow> groupSettings(const QStringList &entries);

// Programs and sources, read through context (its root holds /etc).
void gatherPrograms(const CheckContext &context, Gathered *into);
void gatherSources(const CheckContext &context, Gathered *into);
// The settings rows for the home folder, an absolute path read through
// context, sized with du (spec 4.4).
QList<SettingsRow> gatherSettings(const CheckContext &context, const QString &home);
// Whether a row starts switched on (spec 4.4, INV-2).
bool startsOn(const SettingsRow &row);

// Why Save is unavailable for this many bytes in this folder; empty when
// it is available (spec 4.5, INV-9).
QString saveProblem(qint64 bytes, qint64 freeBytes, const QString &fstype);
struct FolderSpace
{
    qint64 freeBytes = -1; // -1: unknown
    QString fstype;
};
FolderSpace readFolderSpace(const CheckContext &context, const QString &dir);

// groundwork-setup-<hostname>-<YYYY-MM-DD>.tar
QString setupFileName(const QString &hostname, const QDate &date);

QJsonObject manifestJson(const Manifest &manifest);
// Refuses the whole manifest with a reason, or skips single entries,
// saying why in skipped (spec 4.7).
std::optional<Manifest> readManifest(const QJsonObject &json, QString *why, QStringList *skipped);
std::optional<Manifest> readManifest(const QByteArray &json, QString *why, QStringList *skipped);

// Plain words saying why an archive's members do not match its manifest;
// empty when they do: the manifest once, every member a safe path under a
// listed setting, every listed path present (INV-3, INV-5).
QString membersProblem(const QStringList &members, const Manifest &manifest);

// tar's arguments to write and list a saved file (spec 4.5). The settings
// paths go on standard input, NUL-separated.
QStringList createArguments(const QString &partial, const QString &stateDir, const QString &home);
QByteArray createInput(const Manifest &manifest);
QStringList listArguments(const QString &file);

// The names of the settings holding a file or folder under home that this
// user cannot read, which tar leaves out (GRND-0085).
QStringList unreadableSettings(const Manifest &manifest, const QString &home);

// Reads a saved file with tar (spec 4.7).
std::optional<Manifest> readSetupFile(const CheckContext &context, const QString &file, QString *why,
                                      QStringList *skipped);

// For removing secrets (spec 4.8): the members under a secret setting, as
// tar --delete takes them, and the manifest without those settings.
QStringList secretMembers(const QStringList &members, const Manifest &manifest);
Manifest withoutSecrets(const Manifest &manifest);

} // namespace gw
