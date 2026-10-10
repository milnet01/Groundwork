// The smoother scheduler for spinning hard drives (GRND-0054): a udev rule
// gives every spinning drive the bfq scheduler, which keeps the desktop
// responsive while a slow drive is busy. Solid-state drives keep theirs.
// Generalised from the owner's machine (SYSTEM_OPTIMISATIONS.md, section 9),
// which matched one drive by serial number.
#pragma once

#include "core/item.h"

namespace gw {

class DiskSchedulerItem : public Item
{
public:
    QString id() const override { return QStringLiteral("disk-scheduler"); }
    Level level() const override { return Level::SystemSetup; }
    QString title() const override;
    QString applySentence() const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
