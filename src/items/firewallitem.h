// The firewall is on (GRND-0018): firewalld enabled at boot and running.
// The check reads systemctl, because `firewall-cmd --state` is not a
// reliable rootless check (docs/research/2026-10-02-sources.md).
#pragma once

#include "core/item.h"

namespace gw {

class FirewallItem : public Item
{
public:
    QString id() const override { return QStringLiteral("firewall"); }
    Level level() const override { return Level::SystemSetup; }
    QString title() const override;
    QString applySentence() const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
