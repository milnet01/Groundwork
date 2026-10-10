// A shorter wait at the boot menu (GRND-0057): 3 seconds. A menu that
// already waits 3 seconds or less, or is hidden, is left alone. From the
// owner's machine (SYSTEM_OPTIMISATIONS.md, section 6).
//
// systemd-boot and grub2-bls keep the wait in sdbootutil's care: the
// firmware variable LoaderConfigTimeout, which anyone can read, or else a
// file on the root-only EFI partition, which only the Worker's re-check can
// read through sdbootutil. Plain grub2 keeps it in /etc/default/grub.
#pragma once

#include "core/item.h"

namespace gw {

class BootMenuItem : public Item
{
public:
    QString id() const override { return QStringLiteral("boot-menu-wait"); }
    Level level() const override { return Level::SystemSetup; }
    QString title() const override;
    QString applySentence() const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
