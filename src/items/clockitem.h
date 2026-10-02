// The clock setting where Windows is also installed (GRND-0023). Windows
// keeps the hardware clock in local time and Linux in universal time, so
// the clock is wrong after switching (https://itsfoss.com/wrong-time-dual-boot/).
// Groundwork can change only the Linux side: `timedatectl set-local-rtc 1`,
// which man timedatectl warns "is not fully supported".
//
// Windows is found without root from the firmware's boot entries, which
// /sys/firmware/efi/efivars lets anyone read (/boot/efi is root-only);
// without UEFI, from an NTFS partition.
#pragma once

#include "core/item.h"

namespace gw {

class ClockItem : public Item
{
public:
    QString id() const override { return QStringLiteral("dual-boot-clock"); }
    Level level() const override { return Level::Configuration; }
    QString title() const override;
    QString applySentence() const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;

    static bool windowsFound(const CheckContext &context);
};

} // namespace gw
