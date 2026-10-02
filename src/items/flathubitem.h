// Flatpak and Flathub (GRND-0010). Commands from
// https://flathub.org/setup/openSUSE; the check is rootless.
#pragma once

#include "core/item.h"

namespace gw {

class FlathubItem : public Item
{
public:
    QString id() const override { return QStringLiteral("flathub"); }
    Level level() const override { return Level::Essentials; }
    QString title() const override;
    QString applySentence() const override;
    bool installsPackages() const override { return true; }
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps() const override;
};

} // namespace gw
