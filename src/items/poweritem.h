// Laptop power settings (GRND-0020), offered only where the computer has
// its own battery: a power_supply of type Battery whose scope is not
// Device, since a game controller's battery reports scope=Device
// (measured 2026-10-02). Done when a power-profile tool is installed —
// power-profiles-daemon (D-Bus started, so "disabled" yet running is
// normal; measured), tuned-ppd, or TLP, which conflicts with
// power-profiles-daemon (https://linrunner.de/tlp/installation/opensuse.html)
// and is left alone. Apply installs power-profiles-daemon.
#pragma once

#include "core/item.h"

namespace gw {

class PowerItem : public Item
{
public:
    QString id() const override { return QStringLiteral("laptop-power"); }
    Level level() const override { return Level::SystemSetup; }
    QString title() const override;
    QString applySentence() const override;
    bool installsPackages() const override { return true; }
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;

    static bool hasSystemBattery(const CheckContext &context);
};

} // namespace gw
