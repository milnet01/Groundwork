#include "gamingextrasitem.h"

#include <QCoreApplication>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::GamingExtrasItem", text); }

QStringList packages(const CheckContext &context)
{
    QStringList list = {QStringLiteral("steam-devices"), QStringLiteral("gamemoded")};
    if (context.readFile(QStringLiteral("/sys/fs/selinux/enforce")).value_or(QByteArray()).trimmed() == "1")
        list << QStringLiteral("selinux-policy-targeted-gaming");
    return list;
}
} // namespace

QString GamingExtrasItem::title() const { return tr("Game controllers and GameMode"); }

QString GamingExtrasItem::applySentence() const
{
    return tr("Lets Steam reach game controllers and VR headsets, and installs GameMode, which tunes the computer "
              "for games while one runs. Where SELinux is on, it also stops SELinux blocking Windows games.");
}

CheckResult GamingExtrasItem::check(const CheckContext &context) const
{
    for (const QString &package : packages(context)) {
        const CommandResult r = context.run({QStringLiteral("rpm"), QStringLiteral("-q"), package});
        if (!r.started || r.timedOut)
            return {CheckState::CouldNotTell, tr("The installed packages could not be listed.")};
        if (r.exitCode != 0)
            return {CheckState::NotDone, tr("Some of the gaming support is not installed.")};
    }
    return {CheckState::Done, {}};
}

QList<Step> GamingExtrasItem::applySteps(const SystemIdentity &, const CheckContext &context) const
{
    return {{QStringList{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install")} + packages(context),
             true, Step::Tool::Zypper, tr("Installing game controller support and GameMode")}};
}

} // namespace gw
