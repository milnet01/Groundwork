#include "firewallitem.h"

#include "service.h"

#include <QCoreApplication>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::FirewallItem", text); }
const QString kUnit = QStringLiteral("firewalld");
} // namespace

QString FirewallItem::title() const { return tr("Firewall"); }

QString FirewallItem::applySentence() const
{
    return tr("Switches the firewall on now and at every start, so other computers cannot "
              "reach services you did not open.");
}

CheckResult FirewallItem::check(const CheckContext &context) const
{
    const ServiceState s = readService(context, kUnit);
    if (!s.known)
        return {CheckState::CouldNotTell, tr("The service manager did not answer.")};
    if (!s.found)
        return {CheckState::NotDone, tr("The firewall is not installed.")};
    if (s.enabled && s.active)
        return {CheckState::Done, {}};
    return {CheckState::NotDone, tr("The firewall is installed but not switched on.")};
}

QList<Step> FirewallItem::applySteps(const SystemIdentity &, const CheckContext &context) const
{
    QList<Step> steps;
    if (!readService(context, kUnit).found)
        steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"), kUnit},
                      true, Step::Tool::Zypper, tr("Installing the firewall")});
    steps.append({{QStringLiteral("systemctl"), QStringLiteral("enable"), QStringLiteral("--now"), kUnit},
                  true, Step::Tool::Generic, tr("Switching the firewall on")});
    return steps;
}

} // namespace gw
