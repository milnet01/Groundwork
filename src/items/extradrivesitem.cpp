#include "extradrivesitem.h"

#include "core/translations.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <unistd.h>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::ExtraDrivesItem", text); }

const QString kFstab = QStringLiteral("/etc/fstab");
const QString kOptions = QStringLiteral("nofail,noatime,nosuid,nodev,x-gvfs-show");

// Linux filesystems only: Windows drives are left out (user 2026-10-30),
// and FAT or exFAT would need owner options this item does not write.
const QSet<QString> kFilesystems = {QStringLiteral("ext2"), QStringLiteral("ext3"), QStringLiteral("ext4"),
                                    QStringLiteral("xfs"),  QStringLiteral("btrfs"), QStringLiteral("f2fs")};

// Another system's root partition, by the Discoverable Partitions
// Specification's types for x86-64 and AArch64.
const QSet<QString> kRootTypes = {QStringLiteral("4f68bce3-e8cd-4db1-96e7-fbcaf984b709"),
                                  QStringLiteral("b921b045-1df0-41c3-af44-4c6f280d3fae")};

// lsblk prints RM and HOTPLUG as booleans, or as "0" and "1" before
// util-linux 2.37.
bool flag(const QJsonValue &value) { return value.toBool() || value.toString() == QLatin1String("1"); }

struct Fstab
{
    QString text;
    QStringList specs;       // first fields of the lines in use
    QSet<QString> mountPoints;
};

Fstab readFstab(const QByteArray &text)
{
    Fstab fstab{QString::fromUtf8(text), {}, {}};
    for (const QString &line : fstab.text.split(QLatin1Char('\n'))) {
        const QStringList fields = line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() < 2 || fields[0].startsWith(QLatin1Char('#')))
            continue;
        fstab.specs << fields[0];
        fstab.mountPoints << fields[1];
    }
    return fstab;
}

// A UUID anywhere in fstab counts, a commented-out line included: the
// owner comments out a failing drive's line, and that choice stands.
bool listed(const Fstab &fstab, const QJsonObject &device)
{
    const QString uuid = device.value(QStringLiteral("uuid")).toString();
    if (fstab.text.contains(uuid, Qt::CaseInsensitive))
        return true;
    const QString label = device.value(QStringLiteral("label")).toString();
    const QString partuuid = device.value(QStringLiteral("partuuid")).toString();
    QStringList names{device.value(QStringLiteral("path")).toString()};
    if (!label.isEmpty())
        names << QStringLiteral("LABEL=") + label << QStringLiteral("/dev/disk/by-label/") + label;
    if (!partuuid.isEmpty())
        names << QStringLiteral("PARTUUID=") + partuuid << QStringLiteral("/dev/disk/by-partuuid/") + partuuid;
    for (const QString &name : names)
        if (fstab.specs.contains(name, Qt::CaseInsensitive))
            return true;
    return false;
}

// /mnt/<label>, keeping only characters fstab and a shell take as they are.
// A name already used, or a folder that holds files, gets the UUID's start.
QString mountPoint(const CheckContext &context, const QJsonObject &device, const QSet<QString> &taken)
{
    static const QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9._-]"));
    const QString shortId = device.value(QStringLiteral("uuid")).toString().left(8);
    QString name = device.value(QStringLiteral("label")).toString().replace(unsafe, QStringLiteral("_"));
    if (name.isEmpty() || name.startsWith(QLatin1Char('.')))
        name = QStringLiteral("drive-") + shortId;
    const QString path = QStringLiteral("/mnt/") + name;
    if (!taken.contains(path) && context.entries(path, true).isEmpty())
        return path;
    return path + QLatin1Char('-') + shortId;
}
} // namespace

QString ExtraDrivesItem::title() const { return tr("Extra drives connect at start-up"); }

QString ExtraDrivesItem::applySentence() const
{
    return tr("Your other internal drives are ready in /mnt every time the computer starts. A missing drive "
              "never stops it starting.");
}

QList<ExtraDrive> ExtraDrivesItem::missing(const CheckContext &context, QString *why, int *found)
{
    if (found)
        *found = 0;
    const CommandResult r = context.run({QStringLiteral("lsblk"), QStringLiteral("--json"), QStringLiteral("--list"),
                                         QStringLiteral("--output"),
                                         QStringLiteral("NAME,PKNAME,PATH,TYPE,FSTYPE,UUID,PARTUUID,LABEL,PARTTYPE,"
                                                        "MOUNTPOINTS,RM,HOTPLUG,TRAN")});
    const QJsonValue list = QJsonDocument::fromJson(r.out).object().value(QStringLiteral("blockdevices"));
    if (!r.ok() || !list.isArray()) {
        *why = tr("The drives could not be listed.");
        return {};
    }
    const auto fstabText = context.readFile(kFstab);
    if (!fstabText) {
        *why = tr("The list of drives that connect at start-up could not be read.");
        return {};
    }
    const Fstab fstab = readFstab(*fstabText);

    QHash<QString, QJsonObject> byName;
    for (const QJsonValue &v : list.toArray())
        byName.insert(v.toObject().value(QStringLiteral("name")).toString(), v.toObject());

    QList<ExtraDrive> drives;
    QSet<QString> taken = fstab.mountPoints;
    for (const QJsonValue &v : list.toArray()) {
        const QJsonObject device = v.toObject();
        const QString type = device.value(QStringLiteral("type")).toString();
        const QJsonObject disk =
            type == QLatin1String("part") ? byName.value(device.value(QStringLiteral("pkname")).toString()) : device;
        if ((type != QLatin1String("part") && type != QLatin1String("disk"))
            || !kFilesystems.contains(device.value(QStringLiteral("fstype")).toString())
            || device.value(QStringLiteral("uuid")).toString().isEmpty()
            || kRootTypes.contains(device.value(QStringLiteral("parttype")).toString().toLower())
            || flag(device.value(QStringLiteral("rm"))) || flag(device.value(QStringLiteral("hotplug")))
            || flag(disk.value(QStringLiteral("rm"))) || flag(disk.value(QStringLiteral("hotplug")))
            || disk.value(QStringLiteral("tran")).toString() == QLatin1String("usb"))
            continue;
        // Where it is mounted now: under /run/media the desktop opened it
        // when clicked; under /mnt or /media someone set it up; anywhere
        // else it is the system's own, such as / or swap.
        bool openElsewhere = false, byHand = false, system = false;
        for (const QJsonValue &m : device.value(QStringLiteral("mountpoints")).toArray()) {
            const QString point = m.toString();
            if (m.isNull())
                continue;
            if (point.startsWith(QLatin1String("/run/media/")))
                openElsewhere = true;
            else if (point.startsWith(QLatin1String("/mnt/")) || point.startsWith(QLatin1String("/media/")))
                byHand = true;
            else
                system = true;
        }
        if (system)
            continue;
        const bool isListed = listed(fstab, device);
        if (!isListed && byHand)
            continue; // mounted by something other than fstab: never touched
        if (found)
            ++*found;
        if (isListed)
            continue;
        const QString point = mountPoint(context, device, taken);
        taken << point;
        drives << ExtraDrive{device.value(QStringLiteral("path")).toString(), device.value(QStringLiteral("uuid")).toString(),
                             device.value(QStringLiteral("fstype")).toString(), point, openElsewhere};
    }
    why->clear();
    return drives;
}

CheckResult ExtraDrivesItem::check(const CheckContext &context) const
{
    QString why;
    int found = 0;
    const QList<ExtraDrive> drives = missing(context, &why, &found);
    if (!why.isEmpty())
        return {CheckState::CouldNotTell, why};
    if (found == 0)
        return {CheckState::NotNeeded, tr("No extra internal drives were found.")};
    if (drives.isEmpty())
        return {CheckState::Done, {}};
    QStringList names;
    for (const ExtraDrive &d : drives)
        names << isolated(d.mountPoint);
    return {CheckState::NotDone, tr("Not yet connected at start-up: %1.").arg(names.join(QStringLiteral(", ")))};
}

QList<Step> ExtraDrivesItem::applySteps(const SystemIdentity &, const CheckContext &context) const
{
    QString why;
    const QList<ExtraDrive> drives = missing(context, &why);
    if (drives.isEmpty())
        return {};
    const QString label = tr("Connecting extra drives at start-up");
    QStringList points;
    QByteArray lines = "\n# Written by Groundwork: extra drives connect at start-up.\n";
    for (const ExtraDrive &d : drives) {
        points << d.mountPoint;
        lines += QStringLiteral("UUID=%1 %2 %3 %4 0 0\n").arg(d.uuid, d.mountPoint, d.fstype, kOptions).toUtf8();
    }
    QList<Step> steps{makeDirectoriesStep(points, label),
                      {{QStringLiteral("dd"), QStringLiteral("of=") + kFstab, QStringLiteral("oflag=append"),
                        QStringLiteral("conv=notrunc"), QStringLiteral("status=none")},
                       true, Step::Tool::Generic, label, {}, lines},
                      {{QStringLiteral("systemctl"), QStringLiteral("daemon-reload")}, true, Step::Tool::Generic, label}};
    // The Worker runs as the user, so its own ids name them.
    const QString owner = QStringLiteral("%1:%2").arg(::getuid()).arg(::getgid());
    for (const ExtraDrive &d : drives) {
        if (d.openElsewhere)
            continue; // already open on the desktop; its line takes effect at the next start
        steps << Step{{QStringLiteral("mount"), d.mountPoint}, true, Step::Tool::Generic, label};
        // An empty drive becomes the user's; one holding files keeps its owner.
        steps << Step{{QStringLiteral("sh"), QStringLiteral("-c"),
                       QStringLiteral("if [ -z \"$(find \"$1\" -mindepth 1 -maxdepth 1 ! -name lost+found -print "
                                      "-quit)\" ]; then chown \"$2\" \"$1\"; fi"),
                       QStringLiteral("sh"), d.mountPoint, owner},
                      true, Step::Tool::Generic, label};
    }
    return steps;
}

} // namespace gw
