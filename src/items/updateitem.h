// Bringing the system up to date (GRND-0009): a preparation every
// installing item depends on (docs/design.md, The levels).
//
// The check is rootless, so it reads only the last refresh: `zypper
// list-updates` works as a normal user, while `zypper dup --dry-run` needs
// root (measured 2026-10-02, exit 5). It therefore never says "done"; its
// row says what the last refresh listed.
#pragma once

#include "core/item.h"

namespace gw {

class UpdateItem : public Item
{
public:
    QString id() const override { return QStringLiteral("system-update"); }
    Level level() const override { return Level::Essentials; }
    QString title() const override;
    QString applySentence() const override;
    bool isPreparation() const override { return true; }
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
