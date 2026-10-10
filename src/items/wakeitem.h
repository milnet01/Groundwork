// The calmer wake from hibernation (GRND-0055): every daily maintenance
// job that missed its time runs the moment the computer wakes, all at once.
// A random delay on each spreads them out. Only timers without a delay get
// one, so a longer delay the distribution ships is never shortened. From
// the owner's machine (SYSTEM_OPTIMISATIONS.md, section 3).
#pragma once

#include "core/item.h"

namespace gw {

class WakeItem : public Item
{
public:
    QString id() const override { return QStringLiteral("calm-wake"); }
    Level level() const override { return Level::SystemSetup; }
    QString title() const override;
    QString applySentence() const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
