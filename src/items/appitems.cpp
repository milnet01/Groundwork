#include "appitems.h"

#include <QCoreApplication>

namespace gw {

// The catalogue marks each title and sentence with QT_TRANSLATE_NOOP in the
// gw::Apps context, so they are translated where they are shown. Every
// string here names that context itself, so lupdate files it there too.
QString FlathubAppItem::title() const { return QCoreApplication::translate("gw::Apps", m_title); }
QString FlathubAppItem::applySentence() const { return QCoreApplication::translate("gw::Apps", m_sentence); }

CheckResult FlathubAppItem::check(const CheckContext &context) const
{
    const CommandResult r = context.run({QStringLiteral("flatpak"), QStringLiteral("info"), m_appId});
    if (r.ok())
        return {CheckState::Done, {}};
    // Asked even without Flatpak, so an app installed the ordinary way is not offered twice.
    for (const QString &package : m_native)
        if (context.run({QStringLiteral("rpm"), QStringLiteral("-q"), package}).ok())
            return {CheckState::Done, QCoreApplication::translate("gw::Apps", "Installed from the system's software sources.")};
    if (!r.started)
        return {CheckState::NotDone, QCoreApplication::translate("gw::Apps", "Flatpak is not installed.")};
    if (r.timedOut)
        return {CheckState::CouldNotTell, QCoreApplication::translate("gw::Apps", "Flatpak did not answer.")};
    return {CheckState::NotDone, {}};
}

QList<Step> FlathubAppItem::applySteps(const SystemIdentity &, const CheckContext &) const
{
    return {{{QStringLiteral("flatpak"), QStringLiteral("install"), QStringLiteral("-y"),
              QStringLiteral("--noninteractive"), QStringLiteral("--system"), QStringLiteral("flathub"), m_appId},
             true, Step::Tool::Generic, QCoreApplication::translate("gw::Apps", "Installing %1 from Flathub").arg(title())}};
}

QString PackageItem::title() const { return QCoreApplication::translate("gw::Apps", m_title); }
QString PackageItem::applySentence() const { return QCoreApplication::translate("gw::Apps", m_sentence); }

QStringList PackageItem::packages(const SystemIdentity &system) const
{
    return system.family == SystemIdentity::Family::Leap ? m_packages : m_packages + m_rollingOnly;
}

CheckResult PackageItem::check(const CheckContext &context) const
{
    auto release = context.readFile(QStringLiteral("/etc/os-release"));
    if (!release)
        release = context.readFile(QStringLiteral("/usr/lib/os-release"));
    for (const QString &package : packages(parseOsRelease(release.value_or(QByteArray())))) {
        const CommandResult r = context.run({QStringLiteral("rpm"), QStringLiteral("-q"), package});
        if (!r.started || r.timedOut)
            return {CheckState::CouldNotTell, QCoreApplication::translate("gw::Apps", "The installed packages could not be listed.")};
        if (r.exitCode != 0)
            return {CheckState::NotDone, {}};
    }
    return {CheckState::Done, {}};
}

QList<Step> PackageItem::applySteps(const SystemIdentity &system, const CheckContext &) const
{
    return {{QStringList{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install")} + packages(system),
             true, Step::Tool::Zypper, QCoreApplication::translate("gw::Apps", "Installing %1").arg(title())}};
}

} // namespace gw
