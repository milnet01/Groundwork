#include "amdvideoitem.h"

#include "hardware.h"
#include "packman.h"

#include <QCoreApplication>

namespace gw {
namespace {

QString tr(const char *text) { return QCoreApplication::translate("gw::AmdVideoItem", text); }

// An AMD device of the display class: a graphics card (0x0300), a 3D
// controller (0x0302) or another display controller (0x0380).
bool hasAmdGraphics(const CheckContext &context)
{
    for (const PciDevice &d : pciDevices(context)) {
        if (d.vendor == QLatin1String("0x1002") && d.cls.startsWith(QLatin1String("0x03")))
            return true;
    }
    return false;
}

} // namespace

QString AmdVideoItem::title() const { return tr("Video acceleration on AMD graphics"); }

QString AmdVideoItem::applySentence() const
{
    return tr("Installs Packman's video driver for AMD graphics, so videos in every common format play on the "
              "graphics card, which is smoother and saves power.");
}

CheckResult AmdVideoItem::check(const CheckContext &context) const
{
    if (!hasAmdGraphics(context))
        return {CheckState::NotNeeded, tr("No AMD graphics was found.")};
    const CommandResult r = context.run({QStringLiteral("rpm"), QStringLiteral("-q"), QStringLiteral("--qf"),
                                         QStringLiteral("%{VENDOR}"), QStringLiteral("Mesa-libva")});
    if (!r.started || r.timedOut)
        return {CheckState::CouldNotTell, tr("The installed packages could not be listed.")};
    if (r.exitCode == 0 && r.out.toLower().contains("packman"))
        return {CheckState::Done, {}};
    return {CheckState::NotDone, tr("Videos in some common formats are not played on the graphics card yet.")};
}

QList<Step> AmdVideoItem::applySteps(const SystemIdentity &system, const CheckContext &context) const
{
    QString alias;
    QList<Step> steps = packmanSteps(system, context, &alias);
    steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"),
                   QStringLiteral("--allow-vendor-change"), QStringLiteral("--from"), alias,
                   QStringLiteral("Mesa-libva")},
                  true, Step::Tool::Zypper, tr("Installing Packman's video driver for AMD graphics")});
    return steps;
}

} // namespace gw
