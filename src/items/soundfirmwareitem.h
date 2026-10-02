// Sound firmware (GRND-0015): sof-firmware, for audio hardware the
// kernel drives through Sound Open Firmware. Without it such a laptop
// shows only "Dummy Output". Rules from docs/research/2026-10-02-hardware.md.
#pragma once

#include "core/item.h"

namespace gw {

class SoundFirmwareItem : public Item
{
public:
    QString id() const override { return QStringLiteral("sound-firmware"); }
    Level level() const override { return Level::Essentials; }
    QString title() const override;
    QString applySentence() const override;
    bool installsPackages() const override { return true; }
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;

    // Audio hardware that needs Sound Open Firmware: a PCI audio device
    // (class 0x04…) bound to an SOF driver, or an Intel audio DSP (class
    // 0x0401… or 0x040380, kernel intel-dsp-config.c) bound to no driver,
    // which is what a missing firmware leaves.
    static bool needsSof(const CheckContext &context);
};

} // namespace gw
