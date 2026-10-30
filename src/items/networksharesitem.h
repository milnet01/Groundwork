// Share chosen folders on the home network (GRND-0066): Samba and wsdd, with
// no password, per docs/specs/GRND-0066-network-shares.md. This is a security
// boundary: every line written to the shares file comes from a value
// isValidValue accepted and a name shareNames made.
#pragma once

#include "core/item.h"

namespace gw {

class NetworkSharesItem : public Item
{
public:
    QString id() const override { return QStringLiteral("network-shares"); }
    Level level() const override { return Level::Configuration; }
    QString title() const override;
    QString applySentence() const override;
    bool installsPackages() const override { return true; }
    QString valuePrompt() const override;
    bool isValidValue(const QString &value) const override;
    bool valueIsFolderList() const override { return true; }
    QString whyRefused(const QString &path) const override;
    CheckResult check(const CheckContext &context) const override;
    QList<Step> applySteps(const SystemIdentity &system, const CheckContext &context) const override;
    QList<Step> applyStepsFor(const SystemIdentity &system, const CheckContext &context,
                              const QString &value) const override;

    // A share name for each path, none equal, ignoring case, to a name in
    // taken, a name Samba reserves, or an earlier one (spec 4.4).
    static QStringList shareNames(const QStringList &paths, const QStringList &taken);

private:
    // The rules on a path as written (spec 4.3).
    static QString whyPathRefused(const QString &path);
};

} // namespace gw
