// The dual-boot clock item: Windows found from boot entries in the
// efivarfs layout (or NTFS without UEFI), and the RTC setting read from a
// fake timedatectl.
#include "items/catalogue.h"
#include "items/clockitem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

const QString kGuid = QStringLiteral("-8be4df61-93ca-11d2-aa0d-00e098032b8c");

// 4 bytes of efivarfs attributes, then EFI_LOAD_OPTION attributes (4) and
// path-list length (2), then the UTF-16LE description and its NUL.
QByteArray bootEntry(const QString &name)
{
    QByteArray data(10, '\0');
    for (QChar c : name) {
        data.append(char(c.unicode() & 0xff));
        data.append(char(c.unicode() >> 8));
    }
    data.append(2, '\0');
    data.append("\x04\x01", 2); // a device path follows; it must not matter
    return data;
}

} // namespace

class TstClockItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin;
    std::unique_ptr<QTemporaryDir> m_root;
    QByteArray m_oldPath;

    void write(const QString &path, const QByteArray &data, bool exec = false)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).path()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(data);
        f.close();
        if (exec)
            QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
    }
    void efi(const QString &number, const QString &name)
    {
        write(m_root->path() + QStringLiteral("/sys/firmware/efi/efivars/Boot") + number + kGuid, bootEntry(name));
    }
    void localRtc(bool yes)
    {
        write(m_bin.filePath(QStringLiteral("timedatectl")),
              QByteArray("#!/bin/sh\necho LocalRTC=") + (yes ? "yes" : "no") + "\n", true);
    }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }

private slots:
    void initTestCase()
    {
        m_oldPath = qgetenv("PATH");
        qputenv("PATH", m_bin.path().toUtf8());
        write(m_bin.filePath(QStringLiteral("lsblk")), "#!/bin/sh\nprintf 'vfat\\nbtrfs\\n'\n", true);
    }
    void cleanupTestCase() { qputenv("PATH", m_oldPath); }
    void init() { m_root = std::make_unique<QTemporaryDir>(); }

    void noWindowsIsNotNeeded() // the author's machine: openSUSE and "UEFI OS"
    {
        efi(QStringLiteral("0001"), QStringLiteral("openSUSE Boot Manager (grub2-bls)"));
        efi(QStringLiteral("0003"), QStringLiteral("UEFI OS"));
        QCOMPARE(gw::ClockItem().check(context()).state, gw::CheckState::NotNeeded);
    }

    void windowsWithUniversalTimeIsNotDone()
    {
        efi(QStringLiteral("0000"), QStringLiteral("Windows Boot Manager"));
        localRtc(false);
        QCOMPARE(gw::ClockItem().check(context()).state, gw::CheckState::NotDone);
        const auto steps = gw::ClockItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QCOMPARE(steps.size(), 1);
        QCOMPARE(steps[0].argv, QStringList({"timedatectl", "set-local-rtc", "1", "--adjust-system-clock"}));
        QVERIFY(steps[0].needsRoot);
    }

    void windowsWithLocalTimeIsDone()
    {
        efi(QStringLiteral("0000"), QStringLiteral("Windows Boot Manager"));
        localRtc(true);
        QCOMPARE(gw::ClockItem().check(context()).state, gw::CheckState::Done);
    }

    void ignoresOtherEfiVariables()
    {
        // Not a Boot#### entry, though it mentions Windows.
        write(m_root->path() + QStringLiteral("/sys/firmware/efi/efivars/BootOrder") + kGuid, bootEntry(QStringLiteral("Windows")));
        efi(QStringLiteral("0001"), QStringLiteral("openSUSE"));
        QCOMPARE(gw::ClockItem().check(context()).state, gw::CheckState::NotNeeded);
    }

    void withoutUefiAnNtfsPartitionMeansWindows()
    {
        write(m_bin.filePath(QStringLiteral("lsblk")), "#!/bin/sh\nprintf 'ntfs\\next4\\n'\n", true);
        localRtc(false);
        QCOMPARE(gw::ClockItem().check(context()).state, gw::CheckState::NotDone);
        write(m_bin.filePath(QStringLiteral("lsblk")), "#!/bin/sh\nprintf 'vfat\\nbtrfs\\n'\n", true);
        QCOMPARE(gw::ClockItem().check(context()).state, gw::CheckState::NotNeeded);
    }

    void inTheCatalogueInOrder()
    {
        QVERIFY(gw::catalogue().find(QStringLiteral("dual-boot-clock")));
        QVERIFY(gw::catalogue().orderProblems().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstClockItem)
#include "tst_clockitem.moc"
