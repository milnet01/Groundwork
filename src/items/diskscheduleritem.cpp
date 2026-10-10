#include "diskscheduleritem.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::DiskSchedulerItem", text); }

const QString kBlock = QStringLiteral("/sys/block");
const QString kDir = QStringLiteral("/etc/udev/rules.d");
const QString kFile = kDir + QStringLiteral("/60-groundwork-iosched.rules");

// Whole SCSI-family disks only: SATA, SAS and USB drives are all "sd".
// Optical drives (sr) and loop devices also call themselves rotational.
const QByteArray kRule =
    "# Written by Groundwork: the bfq scheduler for every spinning hard drive,\n"
    "# so the desktop stays smooth while one is busy. Remove this file to undo.\n"
    "ACTION==\"add|change\", SUBSYSTEM==\"block\", KERNEL==\"sd*\", ENV{DEVTYPE}==\"disk\", "
    "ATTR{queue/rotational}==\"1\", ATTR{queue/scheduler}=\"bfq\"\n";
} // namespace

QString DiskSchedulerItem::title() const { return tr("Smoother spinning hard drives"); }

QString DiskSchedulerItem::applySentence() const
{
    return tr("Keeps the desktop smooth while a spinning hard drive is busy, by sharing the "
              "drive's time more fairly.");
}

CheckResult DiskSchedulerItem::check(const CheckContext &context) const
{
    static const QRegularExpression active(QStringLiteral("\\[(\\S+)\\]"));
    bool found = false;
    bool allBfq = true;
    for (const QString &name : context.entries(kBlock)) {
        if (!name.startsWith(QLatin1String("sd")))
            continue;
        const QString queue = kBlock + QLatin1Char('/') + name + QStringLiteral("/queue/");
        const auto rotational = context.readFile(queue + QStringLiteral("rotational"));
        if (!rotational)
            return {CheckState::CouldNotTell, tr("A drive's details could not be read.")};
        if (rotational->trimmed() != "1")
            continue;
        found = true;
        const auto scheduler = context.readFile(queue + QStringLiteral("scheduler"));
        if (!scheduler)
            return {CheckState::CouldNotTell, tr("A drive's details could not be read.")};
        if (active.match(QString::fromLatin1(*scheduler)).captured(1) != QLatin1String("bfq"))
            allBfq = false;
    }
    if (!found)
        return {CheckState::NotNeeded, tr("No spinning hard drive was found.")};
    if (allBfq)
        return {CheckState::Done, {}};
    return {CheckState::NotDone, tr("A spinning hard drive uses the standard scheduler.")};
}

QList<Step> DiskSchedulerItem::applySteps(const SystemIdentity &, const CheckContext &) const
{
    const QString label = tr("Setting up the spinning hard drives");
    return {makeDirectoriesStep({kDir}, label),
            writeFileStep(kFile, kRule, label),
            {{QStringLiteral("udevadm"), QStringLiteral("control"), QStringLiteral("--reload")},
             true, Step::Tool::Generic, label},
            {{QStringLiteral("udevadm"), QStringLiteral("trigger"), QStringLiteral("--action=change"),
              QStringLiteral("--subsystem-match=block"), QStringLiteral("--attr-match=queue/rotational=1")},
             true, Step::Tool::Generic, label}};
}

} // namespace gw
