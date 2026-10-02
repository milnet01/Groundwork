// The NVIDIA driver (GRND-0014), offered only where an NVIDIA card is
// found. Rules from docs/research/2026-10-02-hardware.md:
//  - device ids 0x1E02 and above (Turing and newer) get NVIDIA's open
//    driver, G07; 0x1340 to 0x1DF6 (Maxwell to Volta) the proprietary
//    G06; older cards have no current NVIDIA driver;
//  - the kernel module and the user-space driver are installed together,
//    by name. Measured 2026-10-02 (dry runs in containers): on Tumbleweed
//    the G07 module alone resolves without its user space, the state the
//    forum reports leaving the desktop dead; asking for both makes zypper
//    refuse instead, and nothing changes;
//  - the install needs --auto-agree-with-licenses (NVIDIA's licence), so
//    the row says that switching it on accepts the licence.
#pragma once

#include "core/item.h"

namespace gw {

class NvidiaItem : public Item
{
public:
    enum class Driver { None, OpenG07, ProprietaryG06, TooOld };

    QString id() const override { return QStringLiteral("nvidia-driver"); }
    Level level() const override { return Level::Essentials; }
    QString title() const override;
    QString applySentence() const override;
    bool installsPackages() const override { return true; }
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;

    static Driver driverFor(const CheckContext &context);
};

} // namespace gw
