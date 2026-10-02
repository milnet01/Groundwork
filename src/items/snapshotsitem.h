// Btrfs snapshots are on (GRND-0017), so a bad update can be rolled back.
// Without root (measured 2026-10-02): `findmnt -no FSTYPE /` names the root
// filesystem, and `snapper --machine-readable csv list-configs` lists
// "config,subvolume" rows, such as "root,/". Done when a configuration
// covers "/". A root that is not Btrfs has nothing to snapshot.
#pragma once

#include "core/item.h"

namespace gw {

class SnapshotsItem : public Item
{
public:
    QString id() const override { return QStringLiteral("snapshots"); }
    Level level() const override { return Level::SystemSetup; }
    QString title() const override;
    QString applySentence() const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
