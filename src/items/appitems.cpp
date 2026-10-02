#include "appitems.h"

#include <QCoreApplication>

namespace gw {
namespace {
// The catalogue marks each title and sentence with QT_TRANSLATE_NOOP in this
// context, so they are translated where they are shown.
QString tr(const char *text) { return QCoreApplication::translate("gw::Apps", text); }
} // namespace

QString FlathubAppItem::title() const { return tr(m_title); }
QString FlathubAppItem::applySentence() const { return tr(m_sentence); }

CheckResult FlathubAppItem::check(const CheckContext &context) const
{
    const CommandResult r = context.run({QStringLiteral("flatpak"), QStringLiteral("info"), m_appId});
    if (!r.started)
        return {CheckState::NotDone, tr("Flatpak is not installed.")};
    if (r.timedOut)
        return {CheckState::CouldNotTell, tr("Flatpak did not answer.")};
    if (r.exitCode == 0)
        return {CheckState::Done, {}};
    for (const QString &package : m_native)
        if (context.run({QStringLiteral("rpm"), QStringLiteral("-q"), package}).ok())
            return {CheckState::Done, tr("Installed from the system's software sources.")};
    return {CheckState::NotDone, {}};
}

QList<Step> FlathubAppItem::applySteps(const SystemIdentity &, const CheckContext &) const
{
    return {{{QStringLiteral("flatpak"), QStringLiteral("install"), QStringLiteral("-y"),
              QStringLiteral("--noninteractive"), QStringLiteral("--system"), QStringLiteral("flathub"), m_appId},
             true, Step::Tool::Generic, tr("Installing %1 from Flathub").arg(title())}};
}

QString PackageItem::title() const { return tr(m_title); }
QString PackageItem::applySentence() const { return tr(m_sentence); }

CheckResult PackageItem::check(const CheckContext &context) const
{
    for (const QString &package : m_packages) {
        const CommandResult r = context.run({QStringLiteral("rpm"), QStringLiteral("-q"), package});
        if (!r.started || r.timedOut)
            return {CheckState::CouldNotTell, tr("The installed packages could not be listed.")};
        if (r.exitCode != 0)
            return {CheckState::NotDone, {}};
    }
    return {CheckState::Done, {}};
}

QList<Step> PackageItem::applySteps(const SystemIdentity &, const CheckContext &) const
{
    return {{QStringList{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install")} + m_packages,
             true, Step::Tool::Zypper, tr("Installing %1").arg(title())}};
}

} // namespace gw
