// Video acceleration on AMD graphics (GRND-0062), against a fixture sysfs
// tree and a fake rpm first on PATH that answers Mesa-libva's vendor from
// FAKE_MESA_VENDOR, or reports it not installed when that is empty.
#include "items/amdvideoitem.h"
#include "items/catalogue.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstAmdVideoItem : public QObject
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
    void card(const QByteArray &vendor, const QByteArray &cls)
    {
        const QString dir = QStringLiteral("/sys/bus/pci/devices/0000:03:00.0/");
        write(dir + QStringLiteral("vendor"), vendor + '\n');
        write(dir + QStringLiteral("device"), "0x73bf\n");
        write(dir + QStringLiteral("class"), cls + '\n');
    }
    void mesaVendor(const QByteArray &vendor) { qputenv("FAKE_MESA_VENDOR", vendor); }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }
    gw::CheckState state() const { return gw::AmdVideoItem().check(context()).state; }

private slots:
    void initTestCase()
    {
        QFile f(m_bin.filePath(QStringLiteral("rpm")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\n[ \"$*\" = \"-q --qf %{VENDOR} Mesa-libva\" ] || exit 2\n"
                "[ -n \"$FAKE_MESA_VENDOR\" ] || { echo 'package Mesa-libva is not installed'; exit 1; }\n"
                "printf '%s' \"$FAKE_MESA_VENDOR\"\n");
        f.close();
        QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
        m_oldPath = qgetenv("PATH");
        qputenv("PATH", m_bin.path().toUtf8());
    }
    void cleanupTestCase() { qputenv("PATH", m_oldPath); }
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        mesaVendor("");
    }

    void notNeededWithoutAnAmdCard_data()
    {
        QTest::addColumn<QByteArray>("vendor");
        QTest::addColumn<QByteArray>("cls");
        QTest::newRow("no card") << QByteArray() << QByteArray();
        QTest::newRow("NVIDIA card") << QByteArray("0x10de") << QByteArray("0x030000");
        QTest::newRow("AMD audio function") << QByteArray("0x1002") << QByteArray("0x040300");
    }
    void notNeededWithoutAnAmdCard()
    {
        QFETCH(QByteArray, vendor);
        QFETCH(QByteArray, cls);
        if (!vendor.isEmpty())
            card(vendor, cls);
        mesaVendor("http://packman.links2linux.de");
        QCOMPARE(state(), gw::CheckState::NotNeeded);
    }

    void doneOnlyWithPackmansDriver_data()
    {
        QTest::addColumn<QByteArray>("cls");
        QTest::newRow("graphics card") << QByteArray("0x030000");
        QTest::newRow("display controller") << QByteArray("0x038000");
    }
    void doneOnlyWithPackmansDriver()
    {
        QFETCH(QByteArray, cls);
        card("0x1002", cls);
        QCOMPARE(state(), gw::CheckState::NotDone);
        mesaVendor("openSUSE");
        QCOMPARE(state(), gw::CheckState::NotDone);
        mesaVendor("http://packman.links2linux.de");
        QCOMPARE(state(), gw::CheckState::Done);
    }

    void couldNotTellWithoutRpm()
    {
        card("0x1002", "0x030000");
        qputenv("PATH", QByteArray("/nonexistent"));
        const gw::CheckState s = state();
        qputenv("PATH", m_bin.path().toUtf8());
        QCOMPARE(s, gw::CheckState::CouldNotTell);
    }

    // The codecs item adds Packman and switches the system to its builds;
    // this item then installs Packman's driver from the same repository.
    void installsPackmansDriverAfterTheCodecs()
    {
        QCOMPARE(gw::AmdVideoItem().dependsOn(), QStringList{QStringLiteral("media-codecs")});
        write(QStringLiteral("/etc/zypp/repos.d/pm.repo"),
              "[pm]\nbaseurl=https://ftp.fau.de/packman/suse/openSUSE_Tumbleweed/\n");
        const QList<gw::Step> steps = gw::AmdVideoItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QVERIFY(!steps.isEmpty());
        QCOMPARE(steps.last().argv, (QStringList{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"),
                                                 QStringLiteral("--allow-vendor-change"), QStringLiteral("--from"),
                                                 QStringLiteral("pm"), QStringLiteral("Mesa-libva")}));
        for (const gw::Step &s : steps)
            QVERIFY(s.needsRoot && s.tool == gw::Step::Tool::Zypper);
        QVERIFY(gw::catalogue().find(QStringLiteral("amd-video")));
        QVERIFY(gw::catalogue().orderProblems().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstAmdVideoItem)
#include "tst_amdvideoitem.moc"
