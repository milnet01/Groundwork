#include "numlockitem.h"

#include <QCoreApplication>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::NumlockItem", text); }

const QString kLocalDir = QStringLiteral("/etc/sddm.conf.d");

// openSUSE chooses the login screen with an alternative; a system may
// instead link display-manager.service to SDDM's own unit.
bool sddmIsTheLoginScreen(const CheckContext &context)
{
    return context.linkTargetName(QStringLiteral("/etc/alternatives/default-displaymanager")) == QLatin1String("sddm")
        || context.linkTargetName(QStringLiteral("/etc/systemd/system/display-manager.service"))
               == QLatin1String("sddm.service");
}

// SDDM reads its system folder, then the local one, then sddm.conf, each
// folder in file-name order; the last [General] Numlock wins (sddm.conf(5)).
QByteArray numlock(const CheckContext &context)
{
    QStringList files;
    for (const QString &dir : {QStringLiteral("/usr/lib/sddm/sddm.conf.d"), kLocalDir})
        for (const QString &name : context.entries(dir))
            if (name.endsWith(QLatin1String(".conf")))
                files << dir + QLatin1Char('/') + name;
    files << QStringLiteral("/etc/sddm.conf");

    QByteArray value = "none"; // SDDM's default: NumLock is left as it is
    for (const QString &file : files) {
        bool general = false;
        for (const QByteArray &line : context.readFile(file).value_or(QByteArray()).split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.startsWith('['))
                general = trimmed == "[General]";
            else if (general && trimmed.startsWith("Numlock="))
                value = trimmed.mid(int(qstrlen("Numlock="))).trimmed();
        }
    }
    return value;
}
} // namespace

QString NumlockItem::title() const { return tr("NumLock on at login"); }

QString NumlockItem::applySentence() const
{
    return tr("Switches NumLock on at the login screen, so the number pad works straight away.");
}

CheckResult NumlockItem::check(const CheckContext &context) const
{
    if (!sddmIsTheLoginScreen(context))
        return {CheckState::NotNeeded, tr("The login screen is not SDDM, the one this sets up.")};
    if (numlock(context) == "on")
        return {CheckState::Done, {}};
    return {CheckState::NotDone, tr("NumLock is not switched on at the login screen.")};
}

QList<Step> NumlockItem::applySteps(const SystemIdentity &, const CheckContext &) const
{
    const QString label = tr("Switching NumLock on at the login screen");
    return {makeDirectoriesStep({kLocalDir}, label),
            writeFileStep(kLocalDir + QStringLiteral("/99-groundwork-numlock.conf"),
                          "# Written by Groundwork: NumLock on at login. Remove this file to undo.\n"
                          "[General]\nNumlock=on\n",
                          label)};
}

} // namespace gw
