#include "broadcomitem.h"

#include "hardware.h"
#include "packman.h"

#include <QCoreApplication>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::BroadcomItem", text); }
} // namespace

QString BroadcomItem::title() const { return tr("Broadcom Wi-Fi"); }

QString BroadcomItem::applySentence() const
{
    return tr("Installs the driver this computer's Broadcom Wi-Fi card needs. It takes effect "
              "after a restart.");
}

bool BroadcomItem::needsWl(const CheckContext &context)
{
    static const QStringList ids{QStringLiteral("0x4358"), QStringLiteral("0x4359"), QStringLiteral("0x4365"),
                                 QStringLiteral("0x43b1"), QStringLiteral("0x43a0"), QStringLiteral("0x4360")};
    for (const PciDevice &d : pciDevices(context))
        if (d.vendor == QLatin1String("0x14e4") && d.cls.startsWith(QLatin1String("0x0280"))
            && ids.contains(d.device.toLower()))
            return true;
    return false;
}

CheckResult BroadcomItem::check(const CheckContext &context) const
{
    if (!needsWl(context))
        return {CheckState::NotNeeded, tr("No Broadcom Wi-Fi card that needs this driver was found.")};
    if (context.fileExists(QStringLiteral("/sys/module/wl")))
        return {CheckState::Done, {}};
    QString detail = tr("This computer's Wi-Fi card needs a driver that is not installed.");
    if (secureBootOn(context))
        detail += QLatin1Char(' ') + tr("Secure Boot is on, so you may be asked to approve a key "
                                         "at the next restart.");
    return {CheckState::NotDone, detail};
}

QList<Step> BroadcomItem::applySteps(const SystemIdentity &system, const CheckContext &context) const
{
    QList<Step> steps;
    QStringList install{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install")};
    if (system.family == SystemIdentity::Family::Leap) {
        QString alias;
        steps = packmanSteps(system, context, &alias);
        install << QStringLiteral("--from") << alias;
    }
    install << QStringLiteral("broadcom-wl");
    steps.append({install, true, Step::Tool::Zypper, tr("Installing the Broadcom Wi-Fi driver")});
    return steps;
}

} // namespace gw
