// Claude Code (GRND-0082): Anthropic's coding assistant, installed from
// Anthropic's signed RPM repository through zypper, which also keeps it up to
// date. The install was checked on Tumbleweed, Leap 16.0 and Slowroll
// (2026-10-26, the roadmap item's body).
#pragma once

#include "core/item.h"

namespace gw {

class ClaudeCodeItem : public Item
{
public:
    QString id() const override { return QStringLiteral("dev-claude-code"); }
    Level level() const override { return Level::NiceToHave; }
    QString title() const override;
    QString applySentence() const override;
    bool installsPackages() const override { return true; }
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
