// Firmware updates (GRND-0019), through fwupd. Measured 2026-10-02 as a
// normal user: `fwupdmgr get-updates --json` exits 0 and lists devices
// with their releases; the plain form exits 2 with "No updates
// available", fwupdmgr's code for "nothing to do".
#pragma once

#include "core/item.h"

namespace gw {

class FirmwareItem : public Item
{
public:
    QString id() const override { return QStringLiteral("firmware-updates"); }
    Level level() const override { return Level::SystemSetup; }
    QString title() const override;
    QString applySentence() const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
