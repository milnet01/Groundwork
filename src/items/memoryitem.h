// RAM protection (GRND-0052): when memory runs short, the desktop keeps a
// share of it and the most memory-hungry app is closed, before the machine
// freezes. systemd-oomd watches memory pressure; on Tumbleweed and Leap 16
// it ships in systemd-experimental (measured 2026-10-10). The user session
// is given the memory controller, the desktop's session.slice a memory
// floor and more CPU and disk time, and only app.slice may be killed. From
// the owner's machine (SYSTEM_OPTIMISATIONS.md, section 10).
#pragma once

#include "core/item.h"

namespace gw {

class MemoryItem : public Item
{
public:
    QString id() const override { return QStringLiteral("memory-protection"); }
    Level level() const override { return Level::SystemSetup; }
    QString title() const override;
    QString applySentence() const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;

    // The desktop's memory floor for a machine with this much memory, in
    // systemd's units: 2G from 8 GiB, 1G from 4 GiB, else 512M.
    static QString floorFor(qint64 totalKiB);
};

} // namespace gw
