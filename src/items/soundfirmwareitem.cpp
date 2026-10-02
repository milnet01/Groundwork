#include "soundfirmwareitem.h"

#include <QCoreApplication>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::SoundFirmwareItem", text); }
const QString kDevices = QStringLiteral("/sys/bus/pci/devices");
QString field(const CheckContext &context, const QString &device, const char *name)
{
    return QString::fromLatin1(context.readFile(kDevices + QLatin1Char('/') + device + QLatin1Char('/')
                                                + QLatin1String(name))
                                   .value_or(QByteArray()))
        .trimmed();
}
} // namespace

QString SoundFirmwareItem::title() const { return tr("Sound firmware"); }

QString SoundFirmwareItem::applySentence() const
{
    return tr("Installs the firmware some laptops need before their speakers and microphone "
              "work. It takes effect after a restart.");
}

bool SoundFirmwareItem::needsSof(const CheckContext &context)
{
    for (const QString &device : context.entries(kDevices)) {
        const QString cls = field(context, device, "class");
        if (!cls.startsWith(QLatin1String("0x04")))
            continue; // not multimedia
        const QString driver = context.linkTargetName(kDevices + QLatin1Char('/') + device + QStringLiteral("/driver"));
        if (driver.contains(QLatin1String("sof"), Qt::CaseInsensitive))
            return true;
        const bool intelDsp = field(context, device, "vendor") == QLatin1String("0x8086")
            && (cls.startsWith(QLatin1String("0x0401")) || cls == QLatin1String("0x040380"));
        if (intelDsp && driver.isEmpty())
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
