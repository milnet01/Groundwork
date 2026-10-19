// Which settings entries make one app, and which hold secrets (spec 4.4).
// An entry is a path relative to the home folder; a trailing * matches any
// ending. An entry no group claims is a row of its own.
#include "carry.h"

#include <QCoreApplication>

#include <algorithm>

namespace gw {
namespace {

struct Group
{
    QString name;
    QStringList entries;
};

bool entryMatches(const QString &entry, const QString &pattern)
{
    return pattern.endsWith(QLatin1Char('*')) ? entry.startsWith(pattern.chopped(1)) : entry == pattern;
}

bool anyMatches(const QString &entry, const QStringList &patterns)
{
    return std::any_of(patterns.cbegin(), patterns.cend(), [&](const QString &p) { return entryMatches(entry, p); });
}

// Saved passwords, keys, tokens and browser profiles (spec 4.4, "at least").
const QStringList kSecrets{
    QStringLiteral(".ssh"),
    QStringLiteral(".gnupg"),
    QStringLiteral(".pki"),
    QStringLiteral(".password-store"),
    QStringLiteral(".netrc"),
    QStringLiteral(".git-credentials"),
    QStringLiteral(".docker"),
    QStringLiteral(".aws"),
    QStringLiteral(".kube"),
    QStringLiteral(".mozilla"),
    QStringLiteral(".thunderbird"),
    QStringLiteral(".config/google-chrome*"),
    QStringLiteral(".config/chromium"),
    QStringLiteral(".config/BraveSoftware"),
    QStringLiteral(".config/microsoft-edge*"),
    QStringLiteral(".config/vivaldi"),
    QStringLiteral(".config/opera"),
    QStringLiteral(".config/gh"),
    QStringLiteral(".config/gcloud"),
    QStringLiteral(".config/rclone"),
    QStringLiteral(".var/app/org.mozilla.firefox/config"),
    QStringLiteral(".var/app/org.mozilla.Thunderbird/config"),
    QStringLiteral(".var/app/com.google.Chrome/config"),
    QStringLiteral(".var/app/com.brave.Browser/config"),
    QStringLiteral(".var/app/org.chromium.Chromium/config"),
    QStringLiteral(".var/app/com.microsoft.Edge/config"),
    QStringLiteral(".var/app/com.discordapp.Discord/config"),
};

// Built on each call, so the names follow the language loaded.
QList<Group> groups()
{
    return {
        {QCoreApplication::translate("gw::Carry", "KDE Plasma desktop"),
         {QStringLiteral(".config/kdeglobals"), QStringLiteral(".config/kwin*"), QStringLiteral(".config/plasma*"),
          QStringLiteral(".config/kglobalshortcutsrc"), QStringLiteral(".config/khotkeysrc"),
          QStringLiteral(".config/kcminputrc"), QStringLiteral(".config/kxkbrc"), QStringLiteral(".config/ksmserverrc"),
          QStringLiteral(".config/kscreenlockerrc"), QStringLiteral(".config/krunnerrc"),
          QStringLiteral(".config/kactivitymanagerd*"), QStringLiteral(".config/kded5rc"),
          QStringLiteral(".config/kded6rc"), QStringLiteral(".config/ksplashrc"), QStringLiteral(".config/kdedefaults"),
          QStringLiteral(".config/powerdevilrc"), QStringLiteral(".config/powermanagementprofilesrc"),
          QStringLiteral(".config/systemsettingsrc"), QStringLiteral(".config/breezerc"),
          QStringLiteral(".config/kcmfonts"), QStringLiteral(".config/kaccessrc"), QStringLiteral(".config/klaunchrc"),
          QStringLiteral(".config/touchpadxlibinputrc"), QStringLiteral(".config/kiorc"),
          QStringLiteral(".config/kservicemenurc"), QStringLiteral(".config/gtkrc"), QStringLiteral(".config/gtkrc-2.0"),
          QStringLiteral(".config/gtk-3.0"), QStringLiteral(".config/gtk-4.0"), QStringLiteral(".config/xsettingsd"),
          QStringLiteral(".config/Trolltech.conf"), QStringLiteral(".config/autostart"),
          QStringLiteral(".local/share/plasma"), QStringLiteral(".local/share/color-schemes"),
          QStringLiteral(".local/share/kxmlgui5")}},
        {QCoreApplication::translate("gw::Carry", "Your own menu entries"), {QStringLiteral(".local/share/applications"), QStringLiteral(".config/menus")}},
        {QCoreApplication::translate("gw::Carry", "Fonts you added"), {QStringLiteral(".local/share/fonts"), QStringLiteral(".config/fontconfig")}},
        {QStringLiteral("Dolphin"), {QStringLiteral(".config/dolphinrc")}},
        {QStringLiteral("Konsole"), {QStringLiteral(".config/konsolerc"), QStringLiteral(".local/share/konsole")}},
        {QStringLiteral("Kate"), {QStringLiteral(".config/kate*")}},
        {QStringLiteral("Okular"), {QStringLiteral(".config/okular*")}},
        {QStringLiteral("Spectacle"), {QStringLiteral(".config/spectaclerc")}},
        {QStringLiteral("Gwenview"), {QStringLiteral(".config/gwenviewrc")}},
        {QCoreApplication::translate("gw::Carry", "The Bash shell"),
         {QStringLiteral(".bashrc"), QStringLiteral(".bash_profile"), QStringLiteral(".profile"),
          QStringLiteral(".inputrc"), QStringLiteral(".alias")}},
        {QStringLiteral("Git"), {QStringLiteral(".gitconfig"), QStringLiteral(".config/git")}},
        {QStringLiteral("Vim"), {QStringLiteral(".vimrc"), QStringLiteral(".vim")}},
        {QStringLiteral("Firefox"), {QStringLiteral(".mozilla"), QStringLiteral(".var/app/org.mozilla.firefox/config")}},
        {QStringLiteral("Thunderbird"),
         {QStringLiteral(".thunderbird"), QStringLiteral(".var/app/org.mozilla.Thunderbird/config")}},
        {QStringLiteral("Google Chrome"),
         {QStringLiteral(".config/google-chrome*"), QStringLiteral(".var/app/com.google.Chrome/config")}},
        {QStringLiteral("Chromium"),
         {QStringLiteral(".config/chromium"), QStringLiteral(".var/app/org.chromium.Chromium/config")}},
        {QStringLiteral("Brave"),
         {QStringLiteral(".config/BraveSoftware"), QStringLiteral(".var/app/com.brave.Browser/config")}},
        {QStringLiteral("Microsoft Edge"),
         {QStringLiteral(".config/microsoft-edge*"), QStringLiteral(".var/app/com.microsoft.Edge/config")}},
        {QStringLiteral("Discord"), {QStringLiteral(".config/discord"), QStringLiteral(".var/app/com.discordapp.Discord/config")}},
        {QCoreApplication::translate("gw::Carry", "SSH keys"), {QStringLiteral(".ssh")}},
        {QCoreApplication::translate("gw::Carry", "GnuPG keys"), {QStringLiteral(".gnupg")}},
    };
}

} // namespace

QList<SettingsRow> groupSettings(const QStringList &entries)
{
    QList<SettingsRow> rows;
    QStringList claimed;
    for (const Group &group : groups()) {
        SettingsRow row{group.name, {}, false, 0};
        for (const QString &entry : entries) {
            if (!claimed.contains(entry) && anyMatches(entry, group.entries))
                row.paths << entry;
        }
        if (!row.paths.isEmpty()) {
            claimed << row.paths;
            rows << row;
        }
    }
    for (const QString &entry : entries) {
        if (!claimed.contains(entry))
            rows << SettingsRow{entry, {entry}, false, 0};
    }
    for (SettingsRow &row : rows)
        row.secret = std::any_of(row.paths.cbegin(), row.paths.cend(),
                                 [](const QString &path) { return anyMatches(path, kSecrets); });
    return rows;
}

} // namespace gw
