#include "snapshotsitem.h"

#include <QCoreApplication>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::SnapshotsItem", text); }
const QStringList kListConfigs{QStringLiteral("snapper"), QStringLiteral("--machine-readable"),
                               QStringLiteral("csv"), QStringLiteral("list-configs")};
} // namespace

QString SnapshotsItem::title() const { return tr("System snapshots"); }

QString SnapshotsItem::applySentence() const
{
    return tr("Takes a snapshot of the system around every software change, so a change that "
              "goes wrong can be undone.");
}

CheckResult SnapshotsItem::check(const CheckContext &context) const
{
    const CommandResult fs = context.run({QStringLiteral("findmnt"), QStringLiteral("-no"),
                                          QStringLiteral("FSTYPE"), QStringLiteral("/")});
    if (!fs.ok())
        return {CheckState::CouldNotTell, tr("The system's file system could not be read.")};
    if (fs.out.trimmed() != "btrfs")
        return {CheckState::NotNeeded, tr("Snapshots need the Btrfs file system, which this system does not use.")};

    const CommandResult configs = context.run(kListConfigs);
    if (!configs.started)
        return {CheckState::NotDone, tr("The snapshot tool is not installed.")};
    if (!configs.ok())
        return {CheckState::CouldNotTell, tr("The snapshot settings could not be read.")};
    for (const QByteArray &line : configs.out.split('\n'))
        if (line.split(',').value(1).trimmed() == "/")
            return {CheckState::Done, {}};
    return {CheckState::NotDone, tr("Snapshots are not set up for the system.")};
}

QList<Step> SnapshotsItem::applySteps(const SystemIdentity &, const CheckContext &context) const
{
    QList<Step> steps;
    if (!context.run(kListConfigs).started)
        steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"),
                       QStringLiteral("snapper"), QStringLiteral("snapper-zypp-plugin")},
                      true, Step::Tool::Zypper, tr("Installing the snapshot tool")});
    steps.append({{QStringLiteral("snapper"), QStringLiteral("--config"), QStringLiteral("root"),
                   QStringLiteral("create-config"), QStringLiteral("/")},
                  true, Step::Tool::Generic, tr("Setting up snapshots for the system")});
    return steps;
}

} // namespace gw
