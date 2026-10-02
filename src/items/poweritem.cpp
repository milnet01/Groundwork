#include "poweritem.h"

#include <QCoreApplication>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::PowerItem", text); }
const QString kSupplies = QStringLiteral("/sys/class/power_supply");
} // namespace

QString PowerItem::title() const { return tr("Laptop power settings"); }

QString PowerItem::applySentence() const
{
    return tr("Installs power profiles, so you can choose between saving battery and full "
              "speed from the desktop's battery menu.");
}

bool PowerItem::hasSystemBattery(const CheckContext &context)
{
    for (const QString &supply : context.entries(kSupplies)) {
        const QString dir = kSupplies + QLatin1Char('/') + supply + QLatin1Char('/');
        const QByteArray type = context.readFile(dir + QStringLiteral("type")).value_or(QByteArray()).trimmed();
        const QByteArray scope = context.readFile(dir + QStringLiteral("scope")).value_or(QByteArray()).trimmed();
        if (type == "Battery" && scope != "Device")
            return true;
    }
    return false;
}

CheckResult PowerItem::check(const CheckContext &context) const
{
    if (!hasSystemBattery(context))
        return {CheckState::NotNeeded, tr("This computer has no battery.")};
    for (const char *package : {"power-profiles-daemon", "tuned-ppd", "tlp"}) {
        const CommandResult r = context.run({QStringLiteral("rpm"), QStringLiteral("-q"), QLatin1String(package)});
        if (!r.started || r.timedOut)
            return {CheckState::CouldNotTell, tr("The installed packages could not be listed.")};
        if (r.exitCode == 0)
            return {CheckState::Done, {}};
    }
    return {CheckState::NotDone, tr("No power-profile tool is installed.")};
}

QList<Step> PowerItem::applySteps(const SystemIdentity &, const CheckContext &) const
{
    return {{{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"),
              QStringLiteral("power-profiles-daemon")},
             true, Step::Tool::Zypper, tr("Installing power profiles")}};
}

} // namespace gw
