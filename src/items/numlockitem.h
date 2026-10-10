// NumLock on at login (GRND-0060): SDDM's Numlock=on, so the number pad
// works at the login screen. Offered only where SDDM is the login screen.
// From the owner's set-up script.
#pragma once

#include "core/item.h"

namespace gw {

class NumlockItem : public Item
{
public:
    QString id() const override { return QStringLiteral("numlock"); }
    Level level() const override { return Level::Configuration; }
    QString title() const override;
    QString applySentence() const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
