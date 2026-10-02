#include "soundfirmwareitem.h"

#include "hardware.h"

#include <QCoreApplication>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::SoundFirmwareItem", text); }
} // namespace

QString SoundFirmwareItem::title() const { return tr("Sound firmware"); }

QString SoundFirmwareItem::applySentence() const
{
    return tr("Installs the firmware some laptops need before their speakers and microphone "
              "work. It takes effect after a restart.");
}

bool SoundFirmwareItem::needsSof(const CheckContext &context)
{
    for (const PciDevice &d : pciDevices(context)) {
        if (!d.cls.startsWith(QLatin1String("0x04")))
            continue; // not multimedia
        if (d.driver.contains(QLatin1String("sof"), Qt::CaseInsensitive))
            return true;
        const bool intelDsp = d.vendor == QLatin1String("0x8086")
            && (d.cls.startsWith(QLatin1String("0x0401")) || d.cls == QLatin1String("0x040380"));
        if (intelDsp && d.driver.isEmpty())
            return true;
    }
    return false;
}

CheckResult SoundFirmwareItem::check(const CheckContext &context) const
{
    if (!needsSof(context))
        return {CheckState::NotNeeded, tr("This computer's sound works without it.")};
    const CommandResult r = context.run({QStringLiteral("rpm"), QStringLiteral("-q"), QStringLiteral("sof-firmware")});
    if (!r.started || r.timedOut)
        return {CheckState::CouldNotTell, tr("The installed packages could not be listed.")};
    if (r.exitCode == 0)
        return {CheckState::Done, {}};
    return {CheckState::NotDone, tr("This computer's sound needs firmware that is not installed.")};
}

QList<Step> SoundFirmwareItem::applySteps(const SystemIdentity &, const CheckContext &) const
{
    return {{{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"),
              QStringLiteral("sof-firmware")},
             true, Step::Tool::Zypper, tr("Installing the sound firmware")}};
}

} // namespace gw
