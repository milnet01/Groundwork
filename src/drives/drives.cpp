#include "drives.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <algorithm>

namespace gw {
namespace {

QString tr(const char *text) { return QCoreApplication::translate("gw::Drives", text); }

// The sizes of spec 4.3's table, and each use's minimum in a custom layout.
constexpr qint64 kEfiSize = 4 * kGiB;
constexpr qint64 kBiosBootSize = 8 * kMiB;
constexpr qint64 kSwapSize = 2 * kGiB;
constexpr qint64 kSystemMin = 17920 * kMiB;   // 5 GiB x 350 %
constexpr qint64 kSystemWithHome = 53760 * kMiB; // 15 GiB x 350 %
constexpr qint64 kHomeMin = 5 * kGiB;
constexpr qint64 kDataMin = 512 * kMiB;
constexpr qint64 kWholeMin = 24 * kGiB;
constexpr qint64 kWholeWithHomeMin = 64 * kGiB;

const QString kBiosBootType = QStringLiteral("21686148-6449-6E6F-744E-656564454649");

qint64 alignUp(qint64 v) { return (v + kMiB - 1) / kMiB * kMiB; }
qint64 alignDown(qint64 v) { return v / kMiB * kMiB; }

qint64 number(const QJsonValue &v) { return v.toVariant().toLongLong(); }

// GPT keeps its backup at the drive's end: 16 KiB of entries and a header
// sector. The first 1 MiB is left for alignment, as sfdisk does.
qint64 usableEnd(const Drive &drive) { return alignDown(drive.size - 16384 - drive.sectorSize); }

qint64 minimumFor(Use use)
{
    switch (use) {
    case Use::Efi: return kEfiSize;
    case Use::BiosBoot: return kBiosBootSize;
    case Use::Swap: return kSwapSize;
    case Use::System: return kSystemMin;
    case Use::Home: return kHomeMin;
    case Use::Data: return kDataMin;
    }
    return 0;
}

QString sfdiskType(Use use)
{
    switch (use) {
    case Use::Efi: return QStringLiteral("uefi");
    case Use::BiosBoot: return kBiosBootType;
    case Use::Swap: return QStringLiteral("swap");
    case Use::System: return QStringLiteral("linux");
    case Use::Home: return QStringLiteral("home");
    case Use::Data: return QStringLiteral("linux");
    }
    return {};
}

QString partitionName(Use use)
{
    switch (use) {
    case Use::Efi: return QStringLiteral("EFI");
    case Use::BiosBoot: return QStringLiteral("bios");
    case Use::Swap: return QStringLiteral("swap");
    case Use::System: return QStringLiteral("openSUSE");
    case Use::Home: return QStringLiteral("home");
    case Use::Data: return QStringLiteral("data");
    }
    return {};
}

// Where a device is in use, at any depth below a drive (INV-1).
QString inUseBelow(const QJsonObject &device, const QSet<QString> &swaps)
{
    for (const QJsonValue &mount : device.value(QStringLiteral("mountpoints")).toArray()) {
        if (!mount.toString().isEmpty())
            return tr("In use: %1 is mounted at %2.")
                .arg(device.value(QStringLiteral("path")).toString(), mount.toString());
    }
    if (swaps.contains(device.value(QStringLiteral("path")).toString()))
        return tr("In use: %1 is swap.").arg(device.value(QStringLiteral("path")).toString());
    for (const QJsonValue &child : device.value(QStringLiteral("children")).toArray()) {
        if (const QString why = inUseBelow(child.toObject(), swaps); !why.isEmpty())
            return why;
    }
    return {};
}

qint64 restSize(const Layout &layout, qint64 space)
{
    qint64 fixed = 0;
    for (const PlannedPartition &p : layout.partitions)
        fixed += p.size.value_or(0);
    return alignDown(space - fixed);
}

} // namespace

bool Drive::hasWindows() const
{
    return std::any_of(partitions.cbegin(), partitions.cend(), [](const Partition &p) {
        return p.fstype == QLatin1String("ntfs") || p.fstype == QLatin1String("BitLocker");
    });
}

QStringList lsblkArguments()
{
    return {QStringLiteral("lsblk"), QStringLiteral("--json"), QStringLiteral("--bytes"), QStringLiteral("--output"),
            QStringLiteral("NAME,PATH,TYPE,SIZE,START,PTTYPE,PTUUID,PARTTYPE,FSTYPE,LABEL,PARTLABEL,"
                           "MOUNTPOINTS,RM,RO,ROTA,TRAN,MODEL,SERIAL,LOG-SEC")};
}

QList<Drive> parseDrives(const QByteArray &lsblkJson, const QByteArray &swaps)
{
    QSet<QString> swapPaths;
    for (const QByteArray &line : swaps.split('\n').mid(1)) {
        const QByteArray path = line.simplified().split(' ').value(0);
        if (!path.isEmpty())
            swapPaths.insert(QString::fromUtf8(path));
    }
    QList<Drive> drives;
    const QJsonArray devices = QJsonDocument::fromJson(lsblkJson).object().value(QStringLiteral("blockdevices")).toArray();
    for (const QJsonValue &value : devices) {
        const QJsonObject device = value.toObject();
        if (device.value(QStringLiteral("type")).toString() != QLatin1String("disk")
            || device.value(QStringLiteral("ro")).toBool() || number(device.value(QStringLiteral("size"))) <= 0)
            continue;
        Drive drive;
        drive.path = device.value(QStringLiteral("path")).toString();
        drive.model = device.value(QStringLiteral("model")).toString().trimmed();
        drive.serial = device.value(QStringLiteral("serial")).toString().trimmed();
        drive.ptuuid = device.value(QStringLiteral("ptuuid")).toString();
        drive.pttype = device.value(QStringLiteral("pttype")).toString();
        drive.tran = device.value(QStringLiteral("tran")).toString();
        drive.size = number(device.value(QStringLiteral("size")));
        drive.sectorSize = qMax<qint64>(512, number(device.value(QStringLiteral("log-sec"))));
        drive.rotational = device.value(QStringLiteral("rota")).toBool();
        drive.inUse = inUseBelow(device, swapPaths);
        for (const QJsonValue &c : device.value(QStringLiteral("children")).toArray()) {
            const QJsonObject child = c.toObject();
            if (child.value(QStringLiteral("type")).toString() != QLatin1String("part"))
                continue;
            Partition p;
            p.path = child.value(QStringLiteral("path")).toString();
            p.start = number(child.value(QStringLiteral("start"))) * drive.sectorSize;
            p.size = number(child.value(QStringLiteral("size")));
            p.fstype = child.value(QStringLiteral("fstype")).toString();
            p.label = child.value(QStringLiteral("label")).toString();
            p.partlabel = child.value(QStringLiteral("partlabel")).toString();
            p.parttype = child.value(QStringLiteral("parttype")).toString();
            drive.partitions << p;
        }
        std::sort(drive.partitions.begin(), drive.partitions.end(),
                  [](const Partition &a, const Partition &b) { return a.start < b.start; });
        drives << drive;
    }
    return drives;
}

QList<Drive> readDrives(const CheckContext &context)
{
    const CommandResult r = context.run(lsblkArguments());
    if (!r.ok())
        return {};
    return parseDrives(r.out, context.readFile(QStringLiteral("/proc/swaps")).value_or(QByteArray()));
}

Firmware readFirmware(const CheckContext &context)
{
    return context.fileExists(QStringLiteral("/sys/firmware/efi")) ? Firmware::Uefi : Firmware::Bios;
}

QString useName(Use use)
{
    switch (use) {
    case Use::Efi: return QStringLiteral("efi");
    case Use::BiosBoot: return QStringLiteral("bios-boot");
    case Use::Swap: return QStringLiteral("swap");
    case Use::System: return QStringLiteral("system");
    case Use::Home: return QStringLiteral("home");
    case Use::Data: return QStringLiteral("data");
    }
    return {};
}

std::optional<Use> useFromName(const QString &name)
{
    for (Use use : {Use::Efi, Use::BiosBoot, Use::Swap, Use::System, Use::Home, Use::Data}) {
        if (useName(use) == name)
            return use;
    }
    return std::nullopt;
}

FreeSpace largestFreeSpace(const Drive &drive)
{
    FreeSpace best;
    if (drive.pttype != QLatin1String("gpt"))
        return best;
    const qint64 end = usableEnd(drive);
    qint64 cursor = kMiB;
    auto consider = [&](qint64 from, qint64 to) {
        const qint64 start = alignUp(from), stop = alignDown(to);
        if (stop - start > best.size)
            best = {start, stop - start};
    };
    for (const Partition &p : drive.partitions) {
        consider(cursor, p.start);
        cursor = qMax(cursor, p.start + p.size);
    }
    consider(cursor, end);
    return best;
}

qint64 memorySwapSize(const QByteArray &meminfo)
{
    for (const QByteArray &line : meminfo.split('\n')) {
        if (!line.startsWith("MemTotal:"))
            continue;
        const qint64 bytes = line.simplified().split(' ').value(1).toLongLong() * 1024;
        return (bytes + kGiB - 1) / kGiB * kGiB;
    }
    return 0;
}

qint64 usableSpace(const Drive &drive, LayoutKind kind)
{
    return kind == LayoutKind::Beside ? largestFreeSpace(drive).size : usableEnd(drive) - kMiB;
}

bool hibernateStartsOn(qint64 swapSize, qint64 space) { return swapSize * 20 <= space; }

QString suggestionTitle(Suggestion suggestion)
{
    switch (suggestion) {
    case Suggestion::Whole: return tr("The whole drive for openSUSE");
    case Suggestion::WholeWithHome: return tr("The whole drive for openSUSE, with a separate home");
    case Suggestion::Beside: return tr("openSUSE in the free space beside Windows");
    }
    return {};
}

QList<Suggestion> suggestionsFor(const Drive &drive, Firmware, qint64 hibernateSwap)
{
    const qint64 extra = qMax<qint64>(0, hibernateSwap - kSwapSize);
    QList<Suggestion> out;
    if (drive.size >= kWholeMin + extra)
        out << Suggestion::Whole;
    if (drive.size >= kWholeWithHomeMin + extra)
        out << Suggestion::WholeWithHome;
    if (drive.hasWindows() && drive.pttype == QLatin1String("gpt")
        && largestFreeSpace(drive).size >= kWholeMin + extra)
        out << Suggestion::Beside;
    return out;
}

Layout layoutFor(Suggestion suggestion, Firmware firmware, qint64 hibernateSwap)
{
    Layout layout;
    layout.firmware = firmware;
    layout.kind = suggestion == Suggestion::Beside ? LayoutKind::Beside : LayoutKind::Whole;
    layout.partitions << (firmware == Firmware::Uefi ? PlannedPartition{Use::Efi, kEfiSize}
                                                      : PlannedPartition{Use::BiosBoot, kBiosBootSize});
    layout.partitions << PlannedPartition{Use::Swap, qMax(hibernateSwap, kSwapSize)};
    if (suggestion == Suggestion::WholeWithHome) {
        layout.partitions << PlannedPartition{Use::System, kSystemWithHome};
        layout.partitions << PlannedPartition{Use::Home, std::nullopt};
    } else {
        layout.partitions << PlannedPartition{Use::System, std::nullopt};
    }
    return layout;
}

QString validate(const Layout &layout, const Drive &drive)
{
    if (!drive.inUse.isEmpty())
        return drive.inUse;
    if (layout.kind == LayoutKind::Beside && drive.pttype != QLatin1String("gpt"))
        return tr("openSUSE can go beside Windows only on a GPT drive.");
    QHash<Use, int> count;
    int rests = 0;
    qint64 needed = 0;
    for (const PlannedPartition &p : layout.partitions) {
        ++count[p.use];
        if (!p.size) {
            ++rests;
            needed += minimumFor(p.use);
            continue;
        }
        if (*p.size % kMiB != 0)
            return tr("Each size must be a whole number of MiB.");
        if (*p.size < minimumFor(p.use))
            return tr("The %1 partition is smaller than its minimum.").arg(useName(p.use));
        needed += *p.size;
    }
    if (count.value(Use::System) != 1)
        return tr("A layout needs exactly one system partition.");
    const Use boot = layout.firmware == Firmware::Uefi ? Use::Efi : Use::BiosBoot;
    const Use other = layout.firmware == Firmware::Uefi ? Use::BiosBoot : Use::Efi;
    if (count.value(boot) != 1 || count.value(other) != 0)
        return layout.firmware == Firmware::Uefi
                   ? tr("This computer starts with UEFI, so the layout needs one start-up partition.")
                   : tr("This computer starts with BIOS, so the layout needs one BIOS boot partition.");
    for (Use use : {Use::Swap, Use::Home}) {
        if (count.value(use) > 1)
            return tr("A layout may have only one %1 partition.").arg(useName(use));
    }
    if (rests > 1)
        return tr("Only one partition may take the rest of the space.");
    if (needed > usableSpace(drive, layout.kind))
        return tr("The layout does not fit in the space on this drive.");
    return {};
}

QStringList sfdiskArguments(const Layout &layout, const Drive &drive)
{
    if (layout.kind == LayoutKind::Beside)
        return {QStringLiteral("sfdisk"), QStringLiteral("--append"), drive.path};
    return {QStringLiteral("sfdisk"), QStringLiteral("--wipe"), QStringLiteral("always"),
            QStringLiteral("--wipe-partitions"), QStringLiteral("always"), drive.path};
}

QByteArray sfdiskScript(const Layout &layout, const Drive &drive)
{
    QString script;
    if (layout.kind == LayoutKind::Whole) {
        script += QStringLiteral("label: gpt\n");
        const qint64 rest = restSize(layout, usableSpace(drive, layout.kind));
        for (qsizetype i = 0; i < layout.partitions.size(); ++i) {
            const PlannedPartition &p = layout.partitions[i];
            // The last partition taking the rest is left to sfdisk, which
            // knows the drive's last usable sector exactly.
            if (p.size || i + 1 < layout.partitions.size())
                script += QStringLiteral("size=%1MiB, ").arg(p.size.value_or(rest) / kMiB);
            script += QStringLiteral("type=%1, name=%2\n").arg(sfdiskType(p.use), partitionName(p.use));
        }
        return script.toUtf8();
    }
    // Beside Windows: every new partition placed inside the free space,
    // in sectors, so nothing existing moves (INV-5).
    const FreeSpace free = largestFreeSpace(drive);
    const qint64 rest = restSize(layout, free.size);
    qint64 cursor = free.start;
    for (const PlannedPartition &p : layout.partitions) {
        const qint64 size = p.size.value_or(rest);
        script += QStringLiteral("start=%1, size=%2, type=%3, name=%4\n")
                      .arg(cursor / drive.sectorSize)
                      .arg(size / drive.sectorSize)
                      .arg(sfdiskType(p.use), partitionName(p.use));
        cursor += size;
    }
    return script.toUtf8();
}

Format formatFor(Use use)
{
    switch (use) {
    case Use::Efi:
        return {QStringLiteral("vfat"), QStringLiteral("EFI"),
                {QStringLiteral("mkfs.fat"), QStringLiteral("-F"), QStringLiteral("32"), QStringLiteral("-n"), QStringLiteral("EFI")}};
    case Use::BiosBoot: return {};
    case Use::Swap:
        return {QStringLiteral("swap"), QStringLiteral("swap"), {QStringLiteral("mkswap"), QStringLiteral("-L"), QStringLiteral("swap")}};
    case Use::System:
        return {QStringLiteral("btrfs"), QStringLiteral("openSUSE"),
                {QStringLiteral("mkfs.btrfs"), QStringLiteral("-f"), QStringLiteral("-L"), QStringLiteral("openSUSE")}};
    case Use::Home:
        return {QStringLiteral("xfs"), QStringLiteral("home"),
                {QStringLiteral("mkfs.xfs"), QStringLiteral("-f"), QStringLiteral("-L"), QStringLiteral("home")}};
    case Use::Data:
        return {QStringLiteral("xfs"), QStringLiteral("data"),
                {QStringLiteral("mkfs.xfs"), QStringLiteral("-f"), QStringLiteral("-L"), QStringLiteral("data")}};
    }
    return {};
}

QJsonObject layoutFile(const Layout &layout, const Drive &drive)
{
    QJsonArray partitions;
    for (const PlannedPartition &p : layout.partitions)
        partitions.append(QJsonObject{{QStringLiteral("use"), useName(p.use)},
                                      {QStringLiteral("size"), p.size ? QJsonValue(double(*p.size)) : QJsonValue()}});
    return {{QStringLiteral("format"), 1},
            {QStringLiteral("drive"), QJsonObject{{QStringLiteral("path"), drive.path},
                                                 {QStringLiteral("size"), double(drive.size)},
                                                 {QStringLiteral("model"), drive.model},
                                                 {QStringLiteral("serial"), drive.serial},
                                                 {QStringLiteral("ptuuid"), drive.ptuuid}}},
            {QStringLiteral("firmware"), layout.firmware == Firmware::Uefi ? QStringLiteral("uefi") : QStringLiteral("bios")},
            {QStringLiteral("kind"), layout.kind == LayoutKind::Beside ? QStringLiteral("beside") : QStringLiteral("whole")},
            {QStringLiteral("partitions"), partitions}};
}

std::optional<LayoutFile> readLayoutFile(const QByteArray &json, QString *why)
{
    auto refuse = [why](const QString &reason) {
        if (why)
            *why = reason;
        return std::nullopt;
    };
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (root.value(QStringLiteral("format")).toInt() != 1)
        return refuse(tr("The layout file is not one this Groundwork can read."));
    LayoutFile file;
    const QJsonObject drive = root.value(QStringLiteral("drive")).toObject();
    file.drive.path = drive.value(QStringLiteral("path")).toString();
    file.drive.size = number(drive.value(QStringLiteral("size")));
    file.drive.model = drive.value(QStringLiteral("model")).toString();
    file.drive.serial = drive.value(QStringLiteral("serial")).toString();
    file.drive.ptuuid = drive.value(QStringLiteral("ptuuid")).toString();
    if (!file.drive.path.startsWith(QLatin1String("/dev/")) || file.drive.size <= 0)
        return refuse(tr("The layout file names no drive."));
    const QString firmware = root.value(QStringLiteral("firmware")).toString();
    const QString kind = root.value(QStringLiteral("kind")).toString();
    if ((firmware != QLatin1String("uefi") && firmware != QLatin1String("bios"))
        || (kind != QLatin1String("whole") && kind != QLatin1String("beside")))
        return refuse(tr("The layout file is not one this Groundwork can read."));
    file.layout.firmware = firmware == QLatin1String("uefi") ? Firmware::Uefi : Firmware::Bios;
    file.layout.kind = kind == QLatin1String("beside") ? LayoutKind::Beside : LayoutKind::Whole;
    for (const QJsonValue &v : root.value(QStringLiteral("partitions")).toArray()) {
        const QJsonObject p = v.toObject();
        const auto use = useFromName(p.value(QStringLiteral("use")).toString());
        const QJsonValue size = p.value(QStringLiteral("size"));
        if (!use || !(size.isNull() || size.isDouble()) || (size.isDouble() && size.toDouble() <= 0))
            return refuse(tr("The layout file is not one this Groundwork can read."));
        file.layout.partitions << PlannedPartition{*use, size.isNull() ? std::nullopt
                                                                        : std::optional<qint64>(number(size))};
    }
    return file;
}

} // namespace gw
