// Hibernate instead of sleep (GRND-0068), for computers that do not wake
// properly from sleep. From the owner's machine (SYSTEM_OPTIMISATIONS.md,
// section 2). Offered only where the computer already wakes from a swap
// partition that holds all of memory, with Secure Boot off: the item never
// touches the boot loader or the kernel command line, since a wrong resume=
// stops the computer waking (user, 2026-11-06).
#pragma once

#include "core/item.h"

namespace gw {

class HibernateItem : public Item
{
public:
    QString id() const override { return QStringLiteral("hibernate-not-sleep"); }
    Level level() const override { return Level::SystemSetup; }
    QString title() const override;
    QString applySentence() const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
