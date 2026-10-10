// The freeze recorder (GRND-0058): a protected service that samples memory
// and I/O pressure every 10 seconds and writes a line only when pressure
// rises, to the journal and a size-capped ring log,
// /var/log/freeze-recorder.log. After a freeze, that log shows what was
// using the memory. Its script, freeze-recorder.sh, is built into the
// program. From the owner's machine (SYSTEM_OPTIMISATIONS.md, section 11),
// whose recorder has the same file names, so it is found as done there.
#pragma once

#include "core/item.h"

namespace gw {

class FreezeRecorderItem : public Item
{
public:
    QString id() const override { return QStringLiteral("freeze-recorder"); }
    Level level() const override { return Level::SystemSetup; }
    QString title() const override;
    QString applySentence() const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
