// The system log capped at 1 GB (GRND-0056). Without a cap, journald may
// use 10% of the disk, up to 4 GB (journald.conf(5), systemd 261). The cap
// deletes the oldest entries first. From the owner's machine
// (SYSTEM_OPTIMISATIONS.md, section 11).
#pragma once

#include "core/item.h"

namespace gw {

class LogCapItem : public Item
{
public:
    QString id() const override { return QStringLiteral("log-cap"); }
    Level level() const override { return Level::SystemSetup; }
    QString title() const override;
    QString applySentence() const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
