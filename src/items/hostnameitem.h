// The computer's name (GRND-0021): the first item that takes a value.
// Without root (measured 2026-10-02), `hostnamectl hostname --static`
// prints the name. Done when the computer has a name of its own, rather
// than none or "localhost"; renaming a named computer is the desktop
// settings' job. Apply runs `hostnamectl hostname NAME`.
#pragma once

#include "core/item.h"

namespace gw {

class HostnameItem : public Item
{
public:
    QString id() const override { return QStringLiteral("computer-name"); }
    Level level() const override { return Level::Configuration; }
    QString title() const override;
    QString applySentence() const override;
    QString valuePrompt() const override;
    // Letters, digits and hyphens, 1 to 63 of them, not starting or ending
    // with a hyphen: a host name label (RFC 1123).
    bool isValidValue(const QString &value) const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system,
                           const CheckContext &context) const override;
};

} // namespace gw
