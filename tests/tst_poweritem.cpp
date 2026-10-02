// The laptop power item: a computer's own battery versus a device's, and
// any installed power-profile tool as "done".
#include "items/catalogue.h"
#include "items/poweritem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstPowerItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin;
    std::unique_ptr<QTemporaryDir> m_root;
    QByteArray m_oldPath;

    void supply(const QString &name, const QByteArray &type, const QByteArray &scope)
    {
        const QString dir = m_root->path() + QStringLiteral("/sys/class/power_supply/") + name + QLatin1Char('/');
        QVERIFY(QDir().mkpath(dir));
        QFile t(dir + QStringLiteral("type"));
        QVERIFY(t.open(QIODevice::WriteOnly));
        t.write(type + '\n');
        if (!scope.isEmpty()) {
            QFile s(dir + QStringLiteral("scope"));
            QVERIFY(s.open(QIODevice::WriteOnly));
            s.write(scope + '\n');
        }
    }
    void installed(const QByteArray &package) // rpm -q succeeds only for this one
    {
        QFile f(m_bin.filePath(QStringLiteral("rpm")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\n[ \"$2\" = '" + package + "' ] && exit 0\necho \"package $2 is not installed\"; exit 1\n");
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
        installed("nothing");
    }

    void aControllersBatteryIsNotTheComputers() // the author's machine
    {
        supply(QStringLiteral("ps-controller-battery-a4:53:85:7b:29:41"), "Battery", "Device");
        supply(QStringLiteral("AC"), "Mains", QByteArray());
        QCOMPARE(gw::PowerItem().check(context()).state, gw::CheckState::NotNeeded);
    }

    void aLaptopWithoutAToolIsNotDone()
    {
        supply(QStringLiteral("BAT0"), "Battery", QByteArray());
        QCOMPARE(gw::PowerItem().check(context()).state, gw::CheckState::NotDone);
        const auto steps = gw::PowerItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QCOMPARE(steps.size(), 1);
        QCOMPARE(steps[0].argv, QStringList({"zypper", "-n", "install", "power-profiles-daemon"}));
    }

    void anyInstalledToolIsDone_data()
    {
        QTest::addColumn<QByteArray>("package");
        QTest::newRow("power-profiles-daemon") << QByteArray("power-profiles-daemon");
        QTest::newRow("tuned-ppd") << QByteArray("tuned-ppd");
        QTest::newRow("TLP, left alone") << QByteArray("tlp");
    }
    void anyInstalledToolIsDone()
    {
        QFETCH(QByteArray, package);
        supply(QStringLiteral("BAT0"), "Battery", "System");
        installed(package);
        QCOMPARE(gw::PowerItem().check(context()).state, gw::CheckState::Done);
        QVERIFY(gw::catalogue().find(QStringLiteral("laptop-power")));
        QVERIFY(gw::catalogue().orderProblems().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstPowerItem)
#include "tst_poweritem.moc"
