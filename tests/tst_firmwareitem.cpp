// The firmware item against a fake fwupdmgr: JSON with and without
// waiting releases, exit 2 as "nothing to do", and the steps.
#include "items/catalogue.h"
#include "items/firmwareitem.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstFirmwareItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin, m_root;
    QByteArray m_oldPath;

    void fake(const QByteArray &body)
    {
        QFile f(m_bin.filePath(QStringLiteral("fwupdmgr")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\n" + body + "\n");
        f.close();
        QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
    }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root.path())); }

private slots:
    void initTestCase()
    {
        m_oldPath = qgetenv("PATH");
        qputenv("PATH", m_bin.path().toUtf8());
    }
    void cleanupTestCase() { qputenv("PATH", m_oldPath); }

    void noDevicesIsDone() // the author's machine
    {
        fake("printf '{\"Devices\":[]}'");
        QCOMPARE(gw::FirmwareItem().check(context()).state, gw::CheckState::Done);
    }

    void aDeviceWithReleasesIsNotDone()
    {
        fake(R"(printf '{"Devices":[{"Name":"UEFI","Releases":[{"Version":"2"}]},{"Name":"SSD","Releases":[]}]}')");
        const auto r = gw::FirmwareItem().check(context());
        QCOMPARE(r.state, gw::CheckState::NotDone);
        QVERIFY(r.detail.contains(QLatin1String("1")));
    }

    void nothingToDoIsDone()
    {
        fake("echo 'No updates available'; exit 2");
        QCOMPARE(gw::FirmwareItem().check(context()).state, gw::CheckState::Done);
    }

    void otherFailureCouldNotTell()
    {
        fake("exit 1");
        QCOMPARE(gw::FirmwareItem().check(context()).state, gw::CheckState::CouldNotTell);
    }

    void stepsRefreshThenUpdateAndAcceptNothingToDo()
    {
        fake("exit 0");
        const auto steps = gw::FirmwareItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QCOMPARE(steps.size(), 2); // fwupdmgr present: nothing to install
        QCOMPARE(steps[1].argv, QStringList({"fwupdmgr", "update", "-y", "--no-reboot-check"}));
        QVERIFY(steps[1].alsoOk.contains(2));
        QVERIFY(gw::catalogue().find(QStringLiteral("firmware-updates")));
        QVERIFY(gw::catalogue().orderProblems().isEmpty());
    }

    void missingUpdaterIsInstalledFirst()
    {
        QFile::remove(m_bin.filePath(QStringLiteral("fwupdmgr")));
        QCOMPARE(gw::FirmwareItem().check(context()).state, gw::CheckState::NotDone);
        const auto steps = gw::FirmwareItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QCOMPARE(steps[0].argv, QStringList({"zypper", "-n", "install", "fwupd"}));
    }
};

QTEST_GUILESS_MAIN(TstFirmwareItem)
#include "tst_firmwareitem.moc"
