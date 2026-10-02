// Remote login over SSH (GRND-0022), off by default: a choice in
// Configuration, never started on by itself. Leap 16.0 keeps password root
// login over SSH off on new installs (release notes 3.5); this item leaves
// that setting alone.
#pragma once

#include "core/item.h"

namespace gw {

class SshItem : public Item
{
public:
    QString id() const override { return QStringLiteral("ssh"); }
    Level level() const override { return Level::Configuration; }
    QString title() const override;
    QString applySentence() const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
