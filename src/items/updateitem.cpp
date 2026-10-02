#include "updateitem.h"

#include <QCoreApplication>

namespace gw {
namespace {
QString tr(const char *text, int n = -1)
{
    return QCoreApplication::translate("gw::UpdateItem", text, nullptr, n);
}
} // namespace

QString UpdateItem::title() const { return tr("Bring the system up to date"); }

QString UpdateItem::applySentence() const
{
    return tr("Refreshes the software sources and installs every waiting update, so new "
              "software is installed on a current system.");
}

CheckResult UpdateItem::check(const CheckContext &context) const
{
    const CommandResult r = context.run({QStringLiteral("zypper"), QStringLiteral("-n"),
                                         QStringLiteral("--xmlout"), QStringLiteral("list-updates")},
                                        60000);
    if (!r.started)
        return {CheckState::CouldNotTell, tr("zypper, openSUSE's package manager, was not found.")};
    // 106: a repository was skipped. The update check reports what the other
    // repositories offer rather than blocking every install (design, Check
    // results).
    if (r.timedOut || (r.exitCode != 0 && r.exitCode != 106))
        return {CheckState::CouldNotTell, tr("zypper could not list the waiting updates.")};

    const int count = int(r.out.count("<update "));
    QString detail = count > 0
        ? tr("%n update(s) waiting at the last refresh of the software sources.", count)
        : tr("No updates were waiting at the last refresh of the software sources.");
    if (r.exitCode == 106)
        detail += QLatin1Char(' ') + tr("One or more software sources could not be read.");
    return {CheckState::NotDone, detail};
}

QList<Step> UpdateItem::applySteps(const SystemIdentity &system, const CheckContext &) const
{
    // Tumbleweed and Slowroll upgrade with dup; Leap with update.
    const QString command = system.family == SystemIdentity::Family::Leap
        ? QStringLiteral("update") : QStringLiteral("dup");
    return {
        {{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("refresh")},
         true, Step::Tool::Zypper, tr("Refreshing the software sources")},
        {{QStringLiteral("zypper"), QStringLiteral("-n"), command},
         true, Step::Tool::Zypper, tr("Installing the waiting updates")},
    };
}

} // namespace gw
