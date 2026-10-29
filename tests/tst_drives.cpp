// Drives (GRND-0076; docs/specs/GRND-0076-prepare-a-drive.md): which drives
// can be picked, which layouts fit, room to hibernate, and the sfdisk
// script each layout writes. Fixtures are lsblk's JSON, in the shape
// util-linux 2.42.4 prints with --bytes.
#include "drives/drives.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

namespace {

QJsonObject part(const QString &path, qint64 startSector, qint64 size, const QString &fstype = {},
                 const QJsonArray &mounts = {QJsonValue()}, const QJsonArray &children = {})
{
    QJsonObject p{{"name", path.mid(5)}, {"path", path}, {"type", "part"}, {"size", double(size)},
                  {"start", double(startSector)}, {"fstype", fstype.isEmpty() ? QJsonValue() : QJsonValue(fstype)},
                  {"mountpoints", mounts}};
    if (!children.isEmpty())
        p.insert("children", children);
    return p;
}

QJsonObject disk(const QString &path, qint64 size, const QJsonArray &children = {},
                 const QString &pttype = "gpt", bool ro = false)
{
    QJsonObject d{{"name", path.mid(5)}, {"path", path}, {"type", "disk"}, {"size", double(size)},
                  {"start", QJsonValue()}, {"pttype", pttype.isEmpty() ? QJsonValue() : QJsonValue(pttype)},
                  {"ptuuid", "af947acd-e52f-4faf-a236-48385a2a8a58"}, {"mountpoints", QJsonArray{QJsonValue()}},
                  {"rm", false}, {"ro", ro}, {"rota", false}, {"tran", "sata"}, {"model", "Test Disk  "},
                  {"serial", "S123"}, {"log-sec", 512}};
    if (!children.isEmpty())
        d.insert("children", children);
    return d;
}

QByteArray lsblk(const QJsonArray &devices)
{
    return QJsonDocument(QJsonObject{{"blockdevices", devices}}).toJson();
}

gw::Drive one(const QJsonObject &d, const QByteArray &swaps = "Filename Type Size Used Priority\n")
{
    const auto drives = gw::parseDrives(lsblk({d}), swaps);
    return drives.value(0);
}

// A 100 GiB GPT drive with Windows: start-up, reserved, a 40 GiB NTFS
// partition, a gap of about 50 GiB, then a recovery partition. The same
// layout was written to an image with sfdisk to check the numbers
// (2026-10-15).
gw::Drive windowsDrive()
{
    return one(disk("/dev/sdb", 100 * gw::kGiB,
                    {part("/dev/sdb1", 2048, 100 * gw::kMiB, "vfat"),
                     part("/dev/sdb2", 206848, 16 * gw::kMiB),
                     part("/dev/sdb3", 239616, 40 * gw::kGiB, "ntfs"),
                     part("/dev/sdb4", 188743680, 20967424 * qint64(512), "ntfs")}));
}

} // namespace

class TstDrives : public QObject
{
    Q_OBJECT

private slots:
    void readsADrive()
    {
        const gw::Drive d = windowsDrive();
        QCOMPARE(d.path, QStringLiteral("/dev/sdb"));
        QCOMPARE(d.model, QStringLiteral("Test Disk"));
        QCOMPARE(d.partitions.size(), 4);
        QCOMPARE(d.partitions[2].start, qint64(239616) * 512);
        QVERIFY(d.hasWindows());
        QVERIFY(d.inUse.isEmpty());
    }

    // lsblk's START is sysfs's, in 512-byte units whatever the drive's
    // logical sector size.
    void aStartIsIn512ByteUnitsOnAnyDrive()
    {
        QJsonObject d = disk("/dev/sdc", 100 * gw::kGiB, {part("/dev/sdc1", 2048, 100 * gw::kMiB, "vfat")});
        d.insert("log-sec", 4096);
        const gw::Drive drive = one(d);
        QCOMPARE(drive.sectorSize, qint64(4096));
        QCOMPARE(drive.partitions.value(0).start, gw::kMiB);
    }

    void skipsWhatIsNotADrive()
    {
        QJsonObject rom = disk("/dev/sr0", 1 * gw::kGiB);
        rom["type"] = "rom";
        const auto drives = gw::parseDrives(
            lsblk({rom, disk("/dev/sdc", 8 * gw::kGiB, {}, "gpt", true), disk("/dev/sdd", 0)}), {});
        QVERIFY(drives.isEmpty());
    }

    // INV-1: a mounted device at any depth, here an encrypted system's
    // mapping two levels down, makes the drive unpickable.
    void aDriveInUseAtAnyDepthCannotBePicked()
    {
        QJsonObject crypt{{"name", "cr_root"}, {"path", "/dev/mapper/cr_root"}, {"type", "crypt"},
                          {"size", double(50 * gw::kGiB)}, {"mountpoints", QJsonArray{"/"}}};
        const gw::Drive d = one(disk("/dev/nvme0n1", 100 * gw::kGiB,
                                     {part("/dev/nvme0n1p1", 2048, gw::kGiB, "vfat"),
                                      part("/dev/nvme0n1p2", 2099200, 50 * gw::kGiB, "crypto_LUKS",
                                           {QJsonValue()}, {crypt})}));
        QVERIFY(d.inUse.contains(QStringLiteral("/dev/mapper/cr_root")));
        QVERIFY(!gw::validate(gw::layoutFor(gw::Suggestion::Whole, gw::Firmware::Uefi, 0), d).isEmpty());
    }

    void activeSwapCannotBePicked()
    {
        const gw::Drive d = one(disk("/dev/sdb", 100 * gw::kGiB, {part("/dev/sdb1", 2048, 2 * gw::kGiB, "swap")}),
                                "Filename Type Size Used Priority\n/dev/sdb1 partition 2097148 0 -2\n");
        QVERIFY(d.inUse.contains(QStringLiteral("swap")));
    }

    // INV-7: a suggestion is offered only where it fits.
    void suggestionsFit_data()
    {
        QTest::addColumn<qint64>("size");
        QTest::addColumn<qint64>("hibernate");
        QTest::addColumn<bool>("whole");
        QTest::addColumn<bool>("withHome");
        QTest::newRow("23 GiB") << 23 * gw::kGiB << qint64(0) << false << false;
        QTest::newRow("24 GiB") << 24 * gw::kGiB << qint64(0) << true << false;
        QTest::newRow("63 GiB") << 63 * gw::kGiB << qint64(0) << true << false;
        QTest::newRow("64 GiB") << 64 * gw::kGiB << qint64(0) << true << true;
        QTest::newRow("24 GiB, hibernating with 8 GiB") << 24 * gw::kGiB << 8 * gw::kGiB << false << false;
        QTest::newRow("30 GiB, hibernating with 8 GiB") << 30 * gw::kGiB << 8 * gw::kGiB << true << false;
    }

    void suggestionsFit()
    {
        QFETCH(qint64, size);
        QFETCH(qint64, hibernate);
        QFETCH(bool, whole);
        QFETCH(bool, withHome);
        const gw::Drive d = one(disk("/dev/sdb", size));
        const auto offered = gw::suggestionsFor(d, gw::Firmware::Uefi, hibernate);
        QCOMPARE(offered.contains(gw::Suggestion::Whole), whole);
        QCOMPARE(offered.contains(gw::Suggestion::WholeWithHome), withHome);
        QVERIFY(!offered.contains(gw::Suggestion::Beside));
        // Each one offered is a layout validate accepts.
        for (gw::Suggestion s : offered)
            QCOMPARE(gw::validate(gw::layoutFor(s, gw::Firmware::Uefi, hibernate), d), QString());
    }

    // INV-7: beside Windows is judged by the free space, not the drive.
    void besideIsJudgedByTheFreeSpace()
    {
        const gw::Drive d = windowsDrive();
        const gw::FreeSpace free = gw::largestFreeSpace(d);
        QCOMPARE(free.start, qint64(84125696) * 512);
        QCOMPARE(free.size, qint64(188743680 - 84125696) * 512);
        QVERIFY(gw::suggestionsFor(d, gw::Firmware::Uefi, 0).contains(gw::Suggestion::Beside));
        QVERIFY(gw::suggestionsFor(d, gw::Firmware::Uefi, 0).contains(gw::Suggestion::WholeWithHome));
        // Just under 50 GiB is too little with 26 GiB of extra swap.
        QVERIFY(!gw::suggestionsFor(d, gw::Firmware::Uefi, 28 * gw::kGiB).contains(gw::Suggestion::Beside));
    }

    void besideNeedsGpt()
    {
        QJsonObject d = disk("/dev/sdb", 100 * gw::kGiB, {part("/dev/sdb1", 2048, 40 * gw::kGiB, "ntfs")}, "dos");
        QVERIFY(!gw::suggestionsFor(one(d), gw::Firmware::Uefi, 0).contains(gw::Suggestion::Beside));
    }

    // INV-5: a beside layout appends, and every new partition lies in the gap.
    void besideNeverTouchesAnExistingPartition()
    {
        const gw::Drive d = windowsDrive();
        const gw::Layout layout = gw::layoutFor(gw::Suggestion::Beside, gw::Firmware::Uefi, 0);
        QCOMPARE(gw::sfdiskArguments(layout, d), QStringList({"sfdisk", "--append", "/dev/sdb"}));
        const QByteArray script = gw::sfdiskScript(layout, d);
        QCOMPARE(script, QByteArray("start=84125696, size=8388608, type=uefi, name=EFI\n"
                                    "start=92514304, size=4194304, type=swap, name=swap\n"
                                    "start=96708608, size=92035072, type=linux, name=openSUSE\n"));
        for (const QString &line : QString::fromUtf8(script).split('\n', Qt::SkipEmptyParts)) {
            const qint64 start = line.section(' ', 0, 0).mid(6).chopped(1).toLongLong() * 512;
            const qint64 size = line.section(' ', 1, 1).mid(5).chopped(1).toLongLong() * 512;
            QVERIFY(start > 0 && size > 0);
            for (const gw::Partition &p : d.partitions)
                QVERIFY(start + size <= p.start || start >= p.start + p.size);
        }
    }

    // INV-8: the scripts are built from fixed names and computed numbers.
    void wholeDriveScripts()
    {
        const gw::Drive d = one(disk("/dev/sdb", 64 * gw::kGiB));
        const gw::Layout home = gw::layoutFor(gw::Suggestion::WholeWithHome, gw::Firmware::Uefi, 0);
        QCOMPARE(gw::sfdiskArguments(home, d),
                 QStringList({"sfdisk", "--wipe", "always", "--wipe-partitions", "always", "/dev/sdb"}));
        QCOMPARE(gw::sfdiskScript(home, d), QByteArray("label: gpt\n"
                                                       "size=4096MiB, type=uefi, name=EFI\n"
                                                       "size=2048MiB, type=swap, name=swap\n"
                                                       "size=53760MiB, type=linux, name=openSUSE\n"
                                                       "type=home, name=home\n"));
        const gw::Layout bios = gw::layoutFor(gw::Suggestion::Whole, gw::Firmware::Bios, 0);
        QCOMPARE(gw::sfdiskScript(bios, d), QByteArray("label: gpt\n"
                                                       "size=8MiB, type=21686148-6449-6E6F-744E-656564454649, name=bios\n"
                                                       "size=2048MiB, type=swap, name=swap\n"
                                                       "type=linux, name=openSUSE\n"));
    }

    // A custom layout whose rest is not last gets its size worked out.
    void aCustomRestInTheMiddleIsSized()
    {
        const gw::Drive d = one(disk("/dev/sdb", 64 * gw::kGiB));
        gw::Layout custom;
        custom.partitions = {{gw::Use::Efi, 4 * gw::kGiB}, {gw::Use::System, std::nullopt}, {gw::Use::Data, 10 * gw::kGiB}};
        QCOMPARE(gw::validate(custom, d), QString());
        // 64 GiB less 2 MiB for GPT and alignment, 4 GiB and 10 GiB.
        QCOMPARE(gw::sfdiskScript(custom, d), QByteArray("label: gpt\n"
                                                         "size=4096MiB, type=uefi, name=EFI\n"
                                                         "size=51198MiB, type=linux, name=openSUSE\n"
                                                         "size=10240MiB, type=linux, name=data\n"));
    }

    void customLayoutsAreChecked_data()
    {
        using P = gw::PlannedPartition;
        QTest::addColumn<QList<gw::PlannedPartition>>("parts");
        QTest::addColumn<bool>("ok");
        const P efi{gw::Use::Efi, 4 * gw::kGiB}, sys{gw::Use::System, std::nullopt};
        QTest::newRow("start-up and system") << QList<P>{efi, sys} << true;
        QTest::newRow("two systems") << QList<P>{efi, sys, {gw::Use::System, 20 * gw::kGiB}} << false;
        QTest::newRow("no start-up") << QList<P>{sys} << false;
        QTest::newRow("BIOS boot on UEFI") << QList<P>{efi, {gw::Use::BiosBoot, 8 * gw::kMiB}, sys} << false;
        QTest::newRow("two rests") << QList<P>{efi, sys, {gw::Use::Home, std::nullopt}} << false;
        QTest::newRow("two homes") << QList<P>{efi, sys, {gw::Use::Home, 5 * gw::kGiB}, {gw::Use::Home, 5 * gw::kGiB}} << false;
        QTest::newRow("two data") << QList<P>{efi, sys, {gw::Use::Data, gw::kGiB}, {gw::Use::Data, gw::kGiB}} << true;
        QTest::newRow("small start-up") << QList<P>{{gw::Use::Efi, gw::kGiB}, sys} << false;
        QTest::newRow("too big") << QList<P>{efi, {gw::Use::System, 70 * gw::kGiB}} << false;
        QTest::newRow("not whole MiB") << QList<P>{efi, {gw::Use::System, 20 * gw::kGiB + 1}} << false;
        // INV-4: two sizes that add up past qint64 must not wrap round to fit.
        QTest::newRow("sizes past qint64") << QList<P>{efi, {gw::Use::System, qint64(1) << 62}, {gw::Use::Data, qint64(1) << 62}}
                                          << false;
    }

    void customLayoutsAreChecked()
    {
        QFETCH(QList<gw::PlannedPartition>, parts);
        QFETCH(bool, ok);
        gw::Layout layout;
        layout.partitions = parts;
        QCOMPARE(gw::validate(layout, one(disk("/dev/sdb", 64 * gw::kGiB))).isEmpty(), ok);
    }

    // INV-9: room to hibernate starts on when memory-sized swap is at most
    // 5 % of the space the layout uses.
    void roomToHibernateStartsOnBySpace()
    {
        const qint64 swap = gw::memorySwapSize("MemTotal:       32781472 kB\nMemFree: 1 kB\n");
        QCOMPARE(swap, 32 * gw::kGiB);
        const gw::Drive big = one(disk("/dev/sdb", 4000787030016));
        const gw::Drive small = one(disk("/dev/sdb", 256060514304));
        QVERIFY(gw::hibernateStartsOn(swap, gw::usableSpace(big, gw::LayoutKind::Whole)));
        QVERIFY(!gw::hibernateStartsOn(swap, gw::usableSpace(small, gw::LayoutKind::Whole)));
        // Beside Windows the share is of the free space, not the drive.
        const gw::Drive w = windowsDrive();
        QVERIFY(gw::hibernateStartsOn(2 * gw::kGiB, gw::usableSpace(w, gw::LayoutKind::Whole)));
        QVERIFY(!gw::hibernateStartsOn(4 * gw::kGiB, gw::usableSpace(w, gw::LayoutKind::Beside)));
        QVERIFY(gw::hibernateStartsOn(4 * gw::kGiB, gw::usableSpace(w, gw::LayoutKind::Whole)));
    }

    void swapIsNeverBelowItsMinimum()
    {
        const gw::Layout l = gw::layoutFor(gw::Suggestion::Whole, gw::Firmware::Uefi, gw::kGiB);
        QCOMPARE(*l.partitions[1].size, 2 * gw::kGiB);
    }

    void theLayoutFileRoundTrips()
    {
        const gw::Drive d = windowsDrive();
        const gw::Layout layout = gw::layoutFor(gw::Suggestion::Beside, gw::Firmware::Uefi, 0);
        QString why;
        const auto file = gw::readLayoutFile(QJsonDocument(gw::layoutFile(layout, d)).toJson(), &why);
        QVERIFY2(file, qPrintable(why));
        QCOMPARE(file->drive.path, d.path);
        QCOMPARE(file->drive.size, d.size);
        QCOMPARE(file->drive.serial, d.serial);
        QCOMPARE(file->layout.kind, gw::LayoutKind::Beside);
        QCOMPARE(file->layout.partitions.size(), 3);
        QCOMPARE(file->layout.partitions[0].size, std::optional<qint64>(4 * gw::kGiB));
        QVERIFY(!file->layout.partitions[2].size);
    }

    void aDamagedLayoutFileIsRefused()
    {
        QString why;
        QVERIFY(!gw::readLayoutFile("{\"format\":2}", &why));
        QVERIFY(!why.isEmpty());
        QJsonObject f = gw::layoutFile(gw::layoutFor(gw::Suggestion::Whole, gw::Firmware::Uefi, 0),
                                       one(disk("/dev/sdb", 64 * gw::kGiB)));
        QJsonArray parts = f["partitions"].toArray();
        parts.append(QJsonObject{{"use", "boot; rm -rf /"}, {"size", 1}});
        f["partitions"] = parts;
        QVERIFY(!gw::readLayoutFile(QJsonDocument(f).toJson(), &why));
    }

    // INV-4: a hand-written size that is not a whole number of bytes, or
    // does not fit the drive, is refused before any number is taken from it.
    void layoutFileSizesMustBeWholeAndFit_data()
    {
        QTest::addColumn<QJsonValue>("driveSize");
        QTest::addColumn<QJsonValue>("dataSize");
        const QJsonValue drive(double(64 * gw::kGiB));
        QTest::newRow("past qint64") << drive << QJsonValue(1e300);
        QTest::newRow("not whole") << drive << QJsonValue(double(20 * gw::kGiB) + 0.5);
        QTest::newRow("bigger than the drive") << drive << QJsonValue(double(65 * gw::kGiB));
        QTest::newRow("drive past qint64") << QJsonValue(1e300) << QJsonValue(double(20 * gw::kGiB));
    }

    void layoutFileSizesMustBeWholeAndFit()
    {
        QFETCH(QJsonValue, driveSize);
        QFETCH(QJsonValue, dataSize);
        QJsonObject f = gw::layoutFile(gw::layoutFor(gw::Suggestion::Whole, gw::Firmware::Uefi, 0),
                                       one(disk("/dev/sdb", 64 * gw::kGiB)));
        QJsonObject drive = f["drive"].toObject();
        drive["size"] = driveSize;
        f["drive"] = drive;
        QJsonArray parts = f["partitions"].toArray();
        parts.append(QJsonObject{{"use", "data"}, {"size", dataSize}});
        f["partitions"] = parts;
        QString why;
        QVERIFY(!gw::readLayoutFile(QJsonDocument(f).toJson(), &why));
        QVERIFY(!why.isEmpty());
    }

    // GRND-0076's VM run, in Arabic: a path inside right-to-left text came
    // out as "dev/vda5/". Each path is isolated, so it keeps its own
    // direction in any language.
    void pathsKeepTheirOwnDirection()
    {
        gw::Partition p;
        p.path = QStringLiteral("/dev/sdb1");
        for (gw::Use use : {gw::Use::Efi, gw::Use::System, gw::Use::Home}) {
            const QString said = gw::installerAdvice(p, use);
            QVERIFY2(said.contains(QStringLiteral(u"⁨/dev/sdb1⁩")), qPrintable(said));
        }
    }
};

QTEST_GUILESS_MAIN(TstDrives)
#include "tst_drives.moc"
