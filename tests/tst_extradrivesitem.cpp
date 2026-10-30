// Extra drives connect at start-up (GRND-0067), against a fixture root
// holding /etc/fstab and a fake lsblk first on PATH printing a drive list.
#include "items/extradrivesitem.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <unistd.h>

namespace {
// One lsblk --list row; parent is the disk's name for a partition.
QByteArray row(const char *name, const char *parent, const char *type, const char *fstype, const char *uuid,
               const char *label = nullptr, const char *mounted = nullptr, bool hotplug = false,
               const char *tran = "sata", const char *parttype = "0fc63daf-8483-4772-8e79-3d69d8477de4")
{
    auto text = [](const char *s) { return s ? QByteArray("\"") + s + '"' : QByteArray("null"); };
    return QByteArray("{\"name\":") + text(name) + ",\"pkname\":" + text(parent) + ",\"path\":\"/dev/" + name
        + "\",\"type\":" + text(type) + ",\"fstype\":" + text(fstype) + ",\"uuid\":" + text(uuid)
        + ",\"partuuid\":null,\"label\":" + text(label) + ",\"parttype\":" + text(parttype)
        + ",\"mountpoints\":[" + (mounted ? text(mounted) : QByteArray("null")) + "],\"rm\":false,\"hotplug\":"
        + (hotplug ? "true" : "false") + ",\"tran\":" + text(type == QByteArray("disk") ? tran : nullptr) + '}';
}

// The owner's system disk: EFI, root and swap, all its own.
const QByteArray kSystem = row("sda", nullptr, "disk", nullptr, nullptr) + ','
    + row("sda1", "sda", "part", "vfat", "3A34-ED16", nullptr, "/boot/efi", false, "sata",
          "c12a7328-f81f-11d2-ba4b-00a0c93ec93b") + ','
    + row("sda2", "sda", "part", "btrfs", "257f1bea-2290-4d24-96a5-f0e7c377416d", nullptr, "/") + ','
    + row("sda3", "sda", "part", "swap", "3aa9b916-73ce-46c0-8891-59fda4076fe1", nullptr, "[SWAP]");
const QByteArray kGames = row("sdb", nullptr, "disk", "ext4", "86b91583-0c00-418a-8257-fdd28be2daae", "Games");
const QByteArray kSystemFstab = "UUID=257f1bea-2290-4d24-96a5-f0e7c377416d / btrfs defaults 0 0\n";
} // namespace

class TstExtraDrivesItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin;
    QByteArray m_oldPath;
    std::unique_ptr<QTemporaryDir> m_root;

    void write(const QString &path, const QByteArray &text, bool executable = false)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).path()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
        if (executable)
            f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
    }
    void drives(const QByteArray &rows)
    {
        write(m_root->filePath(QStringLiteral("lsblk.json")), "{\"blockdevices\":[" + rows + "]}");
        write(m_bin.filePath(QStringLiteral("lsblk")),
              "#!/bin/sh\nexec /usr/bin/cat '" + m_root->filePath(QStringLiteral("lsblk.json")).toUtf8() + "'\n", true);
    }
    void fstab(const QByteArray &text) { write(m_root->filePath(QStringLiteral("etc/fstab")), text); }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }
    gw::CheckResult check() const { return gw::ExtraDrivesItem().check(context()); }
    QList<gw::ExtraDrive> missing() const
    {
        QString why;
        return gw::ExtraDrivesItem::missing(context(), &why);
    }

private slots:
    void initTestCase()
    {
        m_oldPath = qgetenv("PATH");
        qputenv("PATH", m_bin.path().toUtf8());
    }
    void cleanupTestCase() { qputenv("PATH", m_oldPath); }
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        fstab(kSystemFstab);
        drives(kSystem);
    }

    void notNeededWithOnlyTheSystemDisk() { QCOMPARE(check().state, gw::CheckState::NotNeeded); }

    void couldNotTellWithoutLsblkOrFstab()
    {
        QFile::remove(m_bin.filePath(QStringLiteral("lsblk")));
        QCOMPARE(check().state, gw::CheckState::CouldNotTell);
        drives(kSystem);
        QFile::remove(m_root->filePath(QStringLiteral("etc/fstab")));
        QCOMPARE(check().state, gw::CheckState::CouldNotTell);
    }

    // The owner's machine: the Games drive listed by /dev/disk/by-uuid.
    void doneWhenFstabListsTheDrive_data()
    {
        QTest::addColumn<QByteArray>("line");
        QTest::newRow("by-uuid path")
            << QByteArray("/dev/disk/by-uuid/86b91583-0c00-418a-8257-fdd28be2daae /mnt/Games auto nofail 0 0\n");
        QTest::newRow("UUID=") << QByteArray("UUID=86B91583-0C00-418A-8257-FDD28BE2DAAE /data ext4 defaults 0 2\n");
        QTest::newRow("LABEL=") << QByteArray("LABEL=Games /mnt/Games ext4 defaults 0 2\n");
        QTest::newRow("device path") << QByteArray("/dev/sdb /mnt/Games ext4 defaults 0 2\n");
        // A drive the owner switched off by commenting out its line stays off.
        QTest::newRow("commented out")
            << QByteArray("#OFF# /dev/disk/by-uuid/86b91583-0c00-418a-8257-fdd28be2daae /mnt/Games auto 0 0\n");
    }
    void doneWhenFstabListsTheDrive()
    {
        QFETCH(QByteArray, line);
        drives(kSystem + ',' + kGames);
        QCOMPARE(check().state, gw::CheckState::NotDone);
        fstab(kSystemFstab + line);
        QCOMPARE(check().state, gw::CheckState::Done);
    }

    // The owner's machine as it runs: the listed drive is mounted too,
    // which must not hide it from the count.
    void doneWhenTheListedDriveIsMounted()
    {
        drives(kSystem + ',' + row("sdb", nullptr, "disk", "ext4", "86b91583-0c00-418a-8257-fdd28be2daae", "Games",
                                   "/mnt/Games"));
        fstab(kSystemFstab + "/dev/disk/by-uuid/86b91583-0c00-418a-8257-fdd28be2daae /mnt/Games auto nofail 0 0\n");
        QCOMPARE(check().state, gw::CheckState::Done);
    }

    // A label is not a listing when only a comment carries it.
    void aCommentedLabelDoesNotCount()
    {
        drives(kSystem + ',' + kGames);
        fstab(kSystemFstab + "# LABEL=Games /mnt/Games ext4 defaults 0 2\n");
        QCOMPARE(check().state, gw::CheckState::NotDone);
    }

    // Drives the user chose to leave out (2026-10-30), and the system's own.
    void leftOut_data()
    {
        QTest::addColumn<QByteArray>("rows");
        QTest::newRow("Windows") << (row("sdb", nullptr, "disk", nullptr, nullptr) + ','
                                     + row("sdb1", "sdb", "part", "ntfs", "0A1B2C3D4E5F6071", "Windows"));
        QTest::newRow("USB stick") << row("sdb", nullptr, "disk", "ext4", "1111aaaa-0000-0000-0000-000000000000",
                                          "Stick", nullptr, false, "usb");
        QTest::newRow("hot-plugged") << (row("sdb", nullptr, "disk", nullptr, nullptr, nullptr, nullptr, true) + ','
                                         + row("sdb1", "sdb", "part", "ext4", "2222bbbb-0000-0000-0000-000000000000"));
        QTest::newRow("encrypted") << (row("sdb", nullptr, "disk", nullptr, nullptr) + ','
                                       + row("sdb1", "sdb", "part", "crypto_LUKS", "3333cccc-0000-0000-0000-000000000000"));
        QTest::newRow("unlocked encrypted") << row("dm-0", "sdb1", "crypt", "ext4", "4444dddd-0000-0000-0000-000000000000");
        QTest::newRow("another system's root")
            << (row("sdb", nullptr, "disk", nullptr, nullptr) + ','
                + row("sdb1", "sdb", "part", "btrfs", "5555eeee-0000-0000-0000-000000000000", nullptr, nullptr, false,
                      "sata", "4f68bce3-e8cd-4db1-96e7-fbcaf984b709"));
        QTest::newRow("a system place") << row("sdb", nullptr, "disk", "xfs", "6666ffff-0000-0000-0000-000000000000",
                                               "Data", "/srv/data");
        QTest::newRow("mounted by hand") << row("sdb", nullptr, "disk", "xfs", "7777aaaa-0000-0000-0000-000000000000",
                                                "Data", "/mnt/Data");
        QTest::newRow("no filesystem") << row("sdb", nullptr, "disk", nullptr, nullptr);
    }
    void leftOut()
    {
        QFETCH(QByteArray, rows);
        drives(kSystem + ',' + rows);
        QCOMPARE(check().state, gw::CheckState::NotNeeded);
    }

    void aDriveOpenOnTheDesktopIsAddedButNotMountedNow()
    {
        drives(kSystem + ',' + row("sdb", nullptr, "disk", "ext4", "86b91583-0c00-418a-8257-fdd28be2daae", "Games",
                                   "/run/media/ants/Games"));
        const QList<gw::ExtraDrive> found = missing();
        QCOMPARE(found.size(), 1);
        QVERIFY(found[0].openElsewhere);
        const QList<gw::Step> steps = gw::ExtraDrivesItem().applySteps(gw::SystemIdentity(), context());
        for (const gw::Step &s : steps)
            QVERIFY2(s.argv[0] != QLatin1String("mount"), qPrintable(s.argv.join(' ')));
    }

    void mountPointsComeFromLabels_data()
    {
        QTest::addColumn<QByteArray>("label");
        QTest::addColumn<QString>("expected");
        QTest::newRow("plain") << QByteArray("Games") << QStringLiteral("/mnt/Games");
        QTest::newRow("spaces") << QByteArray("My Films") << QStringLiteral("/mnt/My_Films");
        QTest::newRow("none") << QByteArray() << QStringLiteral("/mnt/drive-86b91583");
        QTest::newRow("dot") << QByteArray("..") << QStringLiteral("/mnt/drive-86b91583");
    }
    void mountPointsComeFromLabels()
    {
        QFETCH(QByteArray, label);
        QFETCH(QString, expected);
        drives(kSystem + ','
               + row("sdb", nullptr, "disk", "ext4", "86b91583-0c00-418a-8257-fdd28be2daae",
                     label.isEmpty() ? nullptr : label.constData()));
        const QList<gw::ExtraDrive> found = missing();
        QCOMPARE(found.size(), 1);
        QCOMPARE(found[0].mountPoint, expected);
    }

    // Two drives with one label, a name fstab already uses, and a folder
    // holding files each get the UUID's start, so nothing is hidden.
    void aTakenNameGetsTheUuid()
    {
        drives(kSystem + ',' + row("sdb", nullptr, "disk", "ext4", "aaaa1111-0000-0000-0000-000000000000", "Data") + ','
               + row("sdc", nullptr, "disk", "ext4", "bbbb2222-0000-0000-0000-000000000000", "Data") + ','
               + row("sdd", nullptr, "disk", "ext4", "cccc3333-0000-0000-0000-000000000000", "Old") + ','
               + row("sde", nullptr, "disk", "ext4", "dddd4444-0000-0000-0000-000000000000", "Full"));
        fstab(kSystemFstab + "/dev/sdz1 /mnt/Old ext4 defaults 0 0\n");
        write(m_root->filePath(QStringLiteral("mnt/Full/keep.txt")), "x");
        const QList<gw::ExtraDrive> found = missing();
        QCOMPARE(found.size(), 4);
        QCOMPARE(found[0].mountPoint, QStringLiteral("/mnt/Data"));
        QCOMPARE(found[1].mountPoint, QStringLiteral("/mnt/Data-bbbb2222"));
        QCOMPARE(found[2].mountPoint, QStringLiteral("/mnt/Old-cccc3333"));
        QCOMPARE(found[3].mountPoint, QStringLiteral("/mnt/Full-dddd4444"));
    }

    void applyAppendsToFstabMountsAndHandsOverAnEmptyDrive()
    {
        drives(kSystem + ',' + kGames);
        const QList<gw::Step> steps = gw::ExtraDrivesItem().applySteps(gw::SystemIdentity(), context());
        QCOMPARE(steps.size(), 5); // folder, fstab, reload, mount, owner
        for (const gw::Step &s : steps)
            QVERIFY(s.needsRoot);
        QCOMPARE(steps[0].argv, (QStringList{"mkdir", "-p", "/mnt/Games"}));
        // Appended, never replaced: the system's own lines stay.
        QVERIFY(steps[1].argv.contains(QStringLiteral("oflag=append")));
        QVERIFY(steps[1].argv.contains(QStringLiteral("conv=notrunc")));
        QVERIFY(steps[1].input.contains("UUID=86b91583-0c00-418a-8257-fdd28be2daae /mnt/Games ext4 "
                                        "nofail,noatime,nosuid,nodev,x-gvfs-show 0 0\n"));
        QCOMPARE(steps[3].argv, (QStringList{"mount", "/mnt/Games"}));
        QCOMPARE(steps[4].argv.mid(3), (QStringList{"sh", "/mnt/Games",
                                                    QStringLiteral("%1:%2").arg(::getuid()).arg(::getgid())}));
    }

    // The owner step's shell, run for real against a folder: an empty one
    // (lost+found aside) changes hands, one holding files does not. A
    // chown to the test's own ids proves the branch through a fake chown.
    void theOwnerStepOnlyTouchesAnEmptyDrive()
    {
        drives(kSystem + ',' + kGames);
        const gw::Step owner = gw::ExtraDrivesItem().applySteps(gw::SystemIdentity(), context()).last();
        write(m_bin.filePath(QStringLiteral("chown")), "#!/bin/sh\necho \"$@\" >> \"$2/chowned\"\n", true);
        write(m_bin.filePath(QStringLiteral("find")), "#!/bin/sh\nexec /usr/bin/find \"$@\"\n", true);
        auto runOn = [&](const QString &dir) {
            QStringList argv = owner.argv;
            argv[argv.size() - 2] = dir;
            QProcess p;
            p.start(QStringLiteral("/bin/sh"), argv.mid(1));
            QVERIFY(p.waitForFinished());
            QCOMPARE(p.exitCode(), 0);
        };
        const QString empty = m_root->filePath(QStringLiteral("empty"));
        QVERIFY(QDir().mkpath(empty + QStringLiteral("/lost+found")));
        runOn(empty);
        QVERIFY(QFile::exists(empty + QStringLiteral("/chowned")));
        const QString full = m_root->filePath(QStringLiteral("full"));
        write(full + QStringLiteral("/game.iso"), "x");
        runOn(full);
        QVERIFY(!QFile::exists(full + QStringLiteral("/chowned")));
    }
};

QTEST_GUILESS_MAIN(TstExtraDrivesItem)
#include "tst_extradrivesitem.moc"
