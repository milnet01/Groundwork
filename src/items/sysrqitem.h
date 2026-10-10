// The emergency keyboard escape (GRND-0053): every Magic SysRq key on, so
// a frozen desktop can be restarted cleanly with Alt+SysRq+R E I S U B, or
// the greediest program closed with Alt+SysRq+F. The kernel handles these
// keys itself, so they work when nothing else on screen responds. From the
// owner's machine (SYSTEM_OPTIMISATIONS.md, section 8).
#pragma once

#include "core/item.h"

namespace gw {

class SysrqItem : public Item
{
public:
    QString id() const override { return QStringLiteral("sysrq"); }
    Level level() const override { return Level::SystemSetup; }
    QString title() const override;
    QString applySentence() const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
