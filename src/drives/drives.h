// Drives (GRND-0076; docs/specs/GRND-0076-prepare-a-drive.md): reading the
// drives, the suggested layouts, and the sfdisk script that writes one.
// Like Items, it names commands and runs nothing itself (docs/design.md,
// What may depend on what).
#pragma once

#include "core/item.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace gw {

constexpr qint64 kMiB = qint64(1) << 20;
constexpr qint64 kGiB = qint64(1) << 30;

struct Partition
{
    QString path;
    qint64 start = 0; // bytes from the drive's start
    qint64 size = 0;  // bytes
    QString fstype, label, partlabel, parttype;
};

struct Drive
{
    QString path, model, serial, ptuuid, pttype, tran;
    qint64 size = 0;           // bytes
    qint64 sectorSize = 512;   // logical
    bool rotational = false;
    QList<Partition> partitions; // in order of start
    QString inUse;             // why it cannot be picked; empty when it can

    bool hasWindows() const;   // an ntfs or BitLocker partition (spec 4.3)
};

// lsblk's arguments (spec 4.1), run through CheckContext::run.
QStringList lsblkArguments();

// Every drive lsblk lists that is a disk, not read-only and not empty.
// Swap is the text of /proc/swaps.
QList<Drive> parseDrives(const QByteArray &lsblkJson, const QByteArray &swaps);
// Empty when lsblk could not run.
QList<Drive> readDrives(const CheckContext &context);

enum class Firmware { Uefi, Bios };
Firmware readFirmware(const CheckContext &context);

enum class Use { Efi, BiosBoot, Swap, System, Home, Data };
QString useName(Use use); // as the layout file writes it
std::optional<Use> useFromName(const QString &name);

struct PlannedPartition
{
    Use use = Use::System;
    std::optional<qint64> size; // bytes; none is "the rest"
};

enum class LayoutKind { Whole, Beside };

struct Layout
{
    LayoutKind kind = LayoutKind::Whole;
    Firmware firmware = Firmware::Uefi;
    QList<PlannedPartition> partitions;
};

struct FreeSpace
{
    qint64 start = 0; // bytes, on a 1 MiB boundary
    qint64 size = 0;  // bytes, a whole number of MiB
};

// The largest free space on a GPT drive, between and around its partitions,
// inside the area GPT leaves usable, aligned to 1 MiB.
FreeSpace largestFreeSpace(const Drive &drive);

// MemTotal from /proc/meminfo, rounded up to a whole GiB; 0 when unreadable.
qint64 memorySwapSize(const QByteArray &meminfo);

// The space a layout may use on this drive: the whole drive, or the
// largest free space beside Windows.
qint64 usableSpace(const Drive &drive, LayoutKind kind);

// Room to hibernate starts on when memory-sized swap is at most 5 % of the
// space the layout uses (spec 4.3, INV-9).
bool hibernateStartsOn(qint64 swapSize, qint64 space);

enum class Suggestion { Whole, WholeWithHome, Beside };
QString suggestionTitle(Suggestion suggestion);

// The suggestions that fit this drive (INV-7). hibernateSwap is the swap
// size with room to hibernate, or 0 when that switch is off.
QList<Suggestion> suggestionsFor(const Drive &drive, Firmware firmware, qint64 hibernateSwap);
Layout layoutFor(Suggestion suggestion, Firmware firmware, qint64 hibernateSwap);

// Plain words saying why the layout cannot go on this drive; empty when it
// can (spec 4.3's rules, which the Worker applies again, INV-4).
QString validate(const Layout &layout, const Drive &drive);

// Each planned partition's size in bytes on this drive, "the rest"
// worked out. Valid layouts only.
QList<qint64> partitionSizes(const Layout &layout, const Drive &drive);

// The partitions a layout written to this drive added, by order of start:
// every partition after a whole-drive layout, the new ones beside Windows.
QList<Partition> addedPartitions(const Layout &layout, const Drive &before, const Drive &after);

// The sfdisk arguments and script that write the layout (spec 4.5). Built
// from fixed names and computed numbers only (INV-8). Valid layouts only.
QStringList sfdiskArguments(const Layout &layout, const Drive &drive);
QByteArray sfdiskScript(const Layout &layout, const Drive &drive);

// The filesystem, its label, and the mkfs command for a new partition;
// an empty command for BIOS boot.
struct Format
{
    QString fstype, label;
    QStringList argv; // the partition's path is appended
};
Format formatFor(Use use);

// The layout file (spec 4.4).
QJsonObject layoutFile(const Layout &layout, const Drive &drive);
struct LayoutFile
{
    Layout layout;
    Drive drive; // path, size, model, serial and ptuuid only
};
std::optional<LayoutFile> readLayoutFile(const QByteArray &json, QString *why);

} // namespace gw
