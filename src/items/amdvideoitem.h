// Video acceleration on AMD graphics (GRND-0062): Packman's Mesa-libva,
// the VA-API driver for AMD cards that also decodes H.264 and HEVC;
// openSUSE's own build leaves those formats out. Offered where an AMD
// display device is found. It depends on the codecs item, which adds the
// full Packman repository and switches Mesa to Packman's builds
// (ADR-0004), so the driver matches the rest of Mesa. From the owner's
// machine.
#pragma once

#include "core/item.h"

namespace gw {

class AmdVideoItem : public Item
{
public:
    QString id() const override { return QStringLiteral("amd-video"); }
    Level level() const override { return Level::Essentials; }
    QString title() const override;
    QString applySentence() const override;
    QStringList dependsOn() const override { return {QStringLiteral("media-codecs")}; }
    bool installsPackages() const override { return true; }
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
