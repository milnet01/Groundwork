// Gaming additions (GRND-0063), beside Steam and Bottles (GRND-0026):
// steam-devices, the controller rules Flatpak Steam needs from the host;
// gamemoded, the GameMode daemon, which Flatpak apps reach through the
// desktop portal's GameMode interface; and, where SELinux is enforcing,
// selinux-policy-targeted-gaming, so Windows games are not blocked. All
// three are in openSUSE's main repository. From the owner's machine.
#pragma once

#include "core/item.h"

namespace gw {

class GamingExtrasItem : public Item
{
public:
    QString id() const override { return QStringLiteral("game-extras"); }
    Level level() const override { return Level::NiceToHave; }
    QString title() const override;
    QString applySentence() const override;
    bool installsPackages() const override { return true; }
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
