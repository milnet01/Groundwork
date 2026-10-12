// OneUp (GRND-0074): the owner's update app, one window that updates the
// system, Flatpak apps and firmware. It sits beside openSUSE's own
// updates and never replaces them. Installed from its OBS repository,
// home:milnet, which builds for Tumbleweed and Leap 16.0 only (OneUp
// README, Install & run).
#pragma once

#include "core/item.h"

namespace gw {

class OneUpItem : public Item
{
public:
    QString id() const override { return QStringLiteral("oneup"); }
    Level level() const override { return Level::NiceToHave; }
    QString title() const override;
    QString applySentence() const override;
    bool installsPackages() const override { return true; }
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
