// The NVIDIA item: which driver a card gets, "done", and the steps per
// system, against fixture sysfs and repos.d trees and a fake rpm.
#include "items/catalogue.h"
#include "items/nvidiaitem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using Driver = gw::NvidiaItem::Driver;
Q_DECLARE_METATYPE(gw::NvidiaItem::Driver)

class TstNvidiaItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin;
    std::unique_ptr<QTemporaryDir> m_root;
    QByteArray m_oldPath;

    void write(const QString &path, const QByteArray &data)
    {
        QVERIFY(QDir().mkpath(QFileInfo(m_root->path() + path).path()));
        QFile f(m_root->path() + path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }
    void card(const QString &slot, const QByteArray &vendor, const QByteArray &device, const QByteArray &cls)
    {
        const QString dir = QStringLiteral("/sys/bus/pci/devices/") + slot + QLatin1Char('/');
        write(dir + QStringLiteral("vendor"), vendor + '\n');
        write(dir + QStringLiteral("device"), device + '\n');
        write(dir + QStringLiteral("class"), cls + '\n');
    }
    void rpmInstalled(bool yes)
    {
        QFile f(m_bin.filePath(QStringLiteral("rpm")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(yes ? "#!/bin/sh\necho installed\n" : "#!/bin/sh\necho 'not installed'\nexit 1\n");
        f.close();
        QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
    }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }

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
        rpmInstalled(false);
    }

    void driverByCard_data()
    {
        QTest::addColumn<QByteArray>("vendor");
        QTest::addColumn<QByteArray>("device");
        QTest::addColumn<QByteArray>("cls");
        QTest::addColumn<Driver>("driver");
        QTest::newRow("RTX 4090, Ada") << QByteArray("0x10de") << QByteArray("0x2684") << QByteArray("0x030000") << Driver::OpenG07;
        QTest::newRow("first Turing id") << QByteArray("0x10de") << QByteArray("0x1e02") << QByteArray("0x030000") << Driver::OpenG07;
        QTest::newRow("GTX 1080, Pascal") << QByteArray("0x10de") << QByteArray("0x1b80") << QByteArray("0x030000") << Driver::ProprietaryG06;
        QTest::newRow("laptop 3D controller") << QByteArray("0x10de") << QByteArray("0x25a2") << QByteArray("0x030200") << Driver::OpenG07;
        QTest::newRow("Kepler, too old") << QByteArray("0x10de") << QByteArray("0x0fc6") << QByteArray("0x030000") << Driver::TooOld;
        QTest::newRow("AMD") << QByteArray("0x1002") << QByteArray("0x73bf") << QByteArray("0x030000") << Driver::None;
        QTest::newRow("NVIDIA audio function") << QByteArray("0x10de") << QByteArray("0x22ba") << QByteArray("0x040300") << Driver::None;
    }
    void driverByCard()
    {
        QFETCH(QByteArray, vendor);
        QFETCH(QByteArray, device);
        QFETCH(QByteArray, cls);
        QFETCH(Driver, driver);
        card(QStringLiteral("0000:01:00.0"), vendor, device, cls);
        QCOMPARE(gw::NvidiaItem::driverFor(context()), driver);
    }

    void theNewestOfSeveralCardsDecides()
    {
        card(QStringLiteral("0000:01:00.0"), "0x10de", "0x1b80", "0x030000");
        card(QStringLiteral("0000:02:00.0"), "0x10de", "0x2684", "0x030000");
        QCOMPARE(gw::NvidiaItem::driverFor(context()), Driver::OpenG07);
    }

    void states()
    {
        QCOMPARE(gw::NvidiaItem().check(context()).state, gw::CheckState::NotNeeded);
        card(QStringLiteral("0000:01:00.0"), "0x10de", "0x2684", "0x030000");
        QCOMPARE(gw::NvidiaItem().check(context()).state, gw::CheckState::NotDone);
        rpmInstalled(true);
        QCOMPARE(gw::NvidiaItem().check(context()).state, gw::CheckState::Done);
        rpmInstalled(false);
        QVERIFY(QDir().mkpath(m_root->path() + QStringLiteral("/sys/module/nvidia")));
        QCOMPARE(gw::NvidiaItem().check(context()).state, gw::CheckState::Done);
    }

    void tumbleweedAddsTheRepositoryAndInstallsTheModuleWithItsUserSpace()
    {
        card(QStringLiteral("0000:01:00.0"), "0x10de", "0x2684", "0x030000");
        const auto steps = gw::NvidiaItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QCOMPARE(steps.size(), 3);
        QCOMPARE(steps[0].argv, QStringList({"zypper", "-n", "addrepo", "--refresh",
                                             "https://download.nvidia.com/opensuse/tumbleweed", "NVIDIA"}));
        QCOMPARE(steps[2].argv, QStringList({"zypper", "-n", "install", "--auto-agree-with-licenses",
                                             "nvidia-open-driver-G07-signed-kmp-meta", "nvidia-userspace-meta-G07"}));
    }

    void leapUsesItsVersionedRepositoryAndTheProprietaryPairForPascal()
    {
        card(QStringLiteral("0000:01:00.0"), "0x10de", "0x1b80", "0x030000");
        const auto steps = gw::NvidiaItem().applySteps(gw::parseOsRelease("ID=opensuse-leap\nVERSION_ID=16.0\n"), context());
        QCOMPARE(steps[0].argv.at(4), QStringLiteral("https://download.nvidia.com/opensuse/leap/$releasever"));
        QCOMPARE(steps[2].argv.mid(4), QStringList({"nvidia-driver-G06-kmp-meta", "nvidia-userspace-meta-G06"}));
    }

    void reusesAConfiguredNvidiaRepository()
    {
        card(QStringLiteral("0000:01:00.0"), "0x10de", "0x2684", "0x030000");
        write(QStringLiteral("/etc/zypp/repos.d/nv.repo"),
              "[repo-nvidia]\nenabled=1\nbaseurl=https://download.nvidia.com/opensuse/tumbleweed\n");
        const auto steps = gw::NvidiaItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QCOMPARE(steps.size(), 2);
        QCOMPARE(steps[0].argv.last(), QStringLiteral("repo-nvidia"));
        QVERIFY(gw::catalogue().find(QStringLiteral("nvidia-driver")));
        QVERIFY(gw::catalogue().orderProblems().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstNvidiaItem)
#include "tst_nvidiaitem.moc"
