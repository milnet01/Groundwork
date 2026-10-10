// Ants Terminal, set as the terminal (GRND-0061): the owner's own terminal,
// installed from its OBS repository, which builds for Tumbleweed and Leap
// 16.0 only (Ants Terminal README, Install). On KDE it is also made the
// terminal the desktop opens, a setting of the user's own, so those steps
// run as the user rather than root.
#pragma once

#include "core/item.h"

namespace gw {

class AntsTerminalItem : public Item
{
public:
    QString id() const override { return QStringLiteral("ants-terminal"); }
    Level level() const override { return Level::NiceToHave; }
    QString title() const override;
    QString applySentence() const override;
    bool installsPackages() const override { return true; }
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
