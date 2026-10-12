// Virtual machines (GRND-0064): openSUSE's kvm_tools pattern, which brings
// QEMU/KVM, libvirt and Virtual Machine Manager (virt-manager). Offered
// where the processor can run them: /proc/cpuinfo lists vmx (Intel) or
// svm (AMD). openSUSE's systemd presets enable libvirt's daemons on
// install, so they run from the next start. From the owner's machine.
#pragma once

#include "core/item.h"

namespace gw {

class VirtualMachinesItem : public Item
{
public:
    QString id() const override { return QStringLiteral("virtual-machines"); }
    Level level() const override { return Level::NiceToHave; }
    QString title() const override;
    QString applySentence() const override;
    bool installsPackages() const override { return true; }
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
