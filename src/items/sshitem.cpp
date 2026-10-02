#include "sshitem.h"

#include "service.h"

#include <QCoreApplication>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::SshItem", text); }
const QString kUnit = QStringLiteral("sshd");
} // namespace

QString SshItem::title() const { return tr("Remote login (SSH)"); }

QString SshItem::applySentence() const
{
    return tr("Lets you log in to this computer from another one over the network, and "
              "lets that through the firewall.");
}

CheckResult SshItem::check(const CheckContext &context) const
{
    const ServiceState s = readService(context, kUnit);
    if (!s.known)
        return {CheckState::CouldNotTell, tr("The service manager did not answer.")};
    if (s.found && s.enabled && s.active)
        return {CheckState::Done, {}};
    return {CheckState::NotDone, tr("Remote login is switched off.")};
}

QList<Step> SshItem::applySteps(const SystemIdentity &, const CheckContext &context) const
{
    QList<Step> steps;
    if (!readService(context, kUnit).found)
        steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"),
                       QStringLiteral("openssh-server")},
                      true, Step::Tool::Zypper, tr("Installing the remote login service")});
    steps.append({{QStringLiteral("systemctl"), QStringLiteral("enable"), QStringLiteral("--now"), kUnit},
                  true, Step::Tool::Generic, tr("Switching remote login on")});
    // With the firewall running, SSH must be let through. Adding a service
    // already allowed counts as success (man firewall-cmd, EXIT CODES).
    if (readService(context, QStringLiteral("firewalld")).active) {
        steps.append({{QStringLiteral("firewall-cmd"), QStringLiteral("--permanent"),
                       QStringLiteral("--add-service=ssh")},
                      true, Step::Tool::Generic, tr("Letting remote login through the firewall")});
        steps.append({{QStringLiteral("firewall-cmd"), QStringLiteral("--reload")},
                      true, Step::Tool::Generic, tr("Reloading the firewall")});
    }
    return steps;
}

} // namespace gw
