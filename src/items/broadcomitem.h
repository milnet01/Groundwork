// Broadcom Wi-Fi (GRND-0016): the broadcom-wl driver, offered only for
// the chips with no open driver (wiki.debian.org/wl: 14e4:4358, 4359,
// 4365, 43b1, 43a0, 4360). The package's own hardware matching claims
// every Broadcom Wi-Fi chip, so the list gates it.
//
// Where the package is: Tumbleweed and Slowroll, openSUSE's NON-OSS
// repository; Leap 16, Packman Essentials
// (docs/research/2026-10-02-hardware.md).
#pragma once

#include "core/item.h"

namespace gw {

class BroadcomItem : public Item
{
public:
    QString id() const override { return QStringLiteral("broadcom-wifi"); }
    Level level() const override { return Level::Essentials; }
    QString title() const override;
    QString applySentence() const override;
    bool installsPackages() const override { return true; }
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;

    static bool needsWl(const CheckContext &context);
};

} // namespace gw
