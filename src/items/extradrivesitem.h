// Extra drives connect at start-up (GRND-0067): every internal drive with
// a Linux filesystem that /etc/fstab does not list gets a line there by
// UUID, with nofail so a missing drive never stops the boot. From the
// owner's machine, whose Games and Emulators drives are mounted this way.
// The design is the roadmap bullet's, settled by the user 2026-10-30.
#pragma once

#include "core/item.h"

namespace gw {

// One drive the item would add, as the check found it.
struct ExtraDrive
{
    QString path, uuid, fstype;
    QString mountPoint; // /mnt/<label>, made unique
    bool openElsewhere = false; // mounted under /run/media: left alone until the next start
};

class ExtraDrivesItem : public Item
{
public:
    QString id() const override { return QStringLiteral("extra-drives"); }
    Level level() const override { return Level::Configuration; }
    QString title() const override;
    QString applySentence() const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;

    // The drives fstab does not list yet. Empty, with why set, when lsblk
    // or fstab could not be read; empty with why empty when there are none.
    // found counts every drive that qualifies, listed or not.
    static QList<ExtraDrive> missing(const CheckContext &context, QString *why, int *found = nullptr);
};

} // namespace gw
