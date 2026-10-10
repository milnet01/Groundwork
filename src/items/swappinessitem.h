// Less eager swapping (GRND-0059): vm.swappiness 10, from the kernel's 60,
// so programs stay in memory longer before moving to disk. A lower value
// already set is left alone. From the owner's machine; the user chose it
// over the recommendation to leave it out (2026-10-10).
#pragma once

#include "core/item.h"

namespace gw {

class SwappinessItem : public Item
{
public:
    QString id() const override { return QStringLiteral("swappiness"); }
    Level level() const override { return Level::SystemSetup; }
    QString title() const override;
    QString applySentence() const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
