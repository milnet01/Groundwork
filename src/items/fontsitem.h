// Microsoft-compatible fonts (GRND-0029), through fetchmsttfonts, which
// downloads Microsoft's core web fonts while it installs. Measured
// 2026-10-02: on Tumbleweed it installs without a prompt (dry run in a
// container); on Leap 16.0 zypper reports it not found (exit 104). Done
// when the fonts are present (`fc-list` lists Arial), whatever put them
// there.
#pragma once

#include "core/item.h"

namespace gw {

class FontsItem : public Item
{
public:
    QString id() const override { return QStringLiteral("microsoft-fonts"); }
    Level level() const override { return Level::NiceToHave; }
    QString title() const override;
    QString applySentence() const override;
    bool installsPackages() const override { return true; }
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
