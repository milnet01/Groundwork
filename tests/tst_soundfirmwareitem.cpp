// The sound firmware item against a fixture sysfs tree (class, vendor and
// a driver symlink per device) and a fake rpm.
#include "items/catalogue.h"
#include "items/soundfirmwareitem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstSoundFirmwareItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin;
    std::unique_ptr<QTemporaryDir> m_root;
    QByteArray m_oldPath;

    void device(const QString &slot, const QByteArray &vendor, const QByteArray &cls, const QString &driver)
    {
        const QString dir = m_root->path() + QStringLiteral("/sys/bus/pci/devices/") + slot;
        QVERIFY(QDir().mkpath(dir));
        for (const auto &[name, value] : {std::pair{"vendor", vendor}, std::pair{"class", cls}}) {
            QFile f(dir + QLatin1Char('/') + QLatin1String(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(value + '\n');
        }
        if (!driver.isEmpty()) {
            const QString drivers = m_root->path() + QStringLiteral("/sys/bus/pci/drivers/") + driver;
            QVERIFY(QDir().mkpath(drivers));
            QVERIFY(QFile::link(drivers, dir + QStringLiteral("/driver")));
        }
    }
    void rpmSays(bool installed)
    {
        QFile f(m_bin.filePath(QStringLiteral("rpm")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(installed ? "#!/bin/sh\necho sof-firmware-2026.09.1\n" : "#!/bin/sh\necho 'package sof-firmware is not installed'\nexit 1\n");
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
    void init() { m_root = std::make_unique<QTemporaryDir>(); }

    void hdAudioIsNotNeeded() // the author's machine: AMD, snd_hda_intel
    {
        device(QStringLiteral("0000:30:00.1"), "0x1002", "0x040300", QStringLiteral("snd_hda_intel"));
        rpmSays(false);
        QCOMPARE(gw::SoundFirmwareItem().check(context()).state, gw::CheckState::NotNeeded);
    }

    void sofDriverWithoutFirmwareIsNotDone()
    {
        device(QStringLiteral("0000:00:1f.3"), "0x8086", "0x040100", QStringLiteral("sof-audio-pci-intel-tgl"));
        rpmSays(false);
        QCOMPARE(gw::SoundFirmwareItem().check(context()).state, gw::CheckState::NotDone);
    }

    void intelDspWithNoDriverIsNotDone() // what a missing firmware leaves
    {
        device(QStringLiteral("0000:00:1f.3"), "0x8086", "0x040380", QString());
        rpmSays(false);
        QCOMPARE(gw::SoundFirmwareItem().check(context()).state, gw::CheckState::NotDone);
    }

    void installedFirmwareIsDone()
    {
        device(QStringLiteral("0000:00:1f.3"), "0x8086", "0x040100", QStringLiteral("sof-audio-pci-intel-tgl"));
        rpmSays(true);
        QCOMPARE(gw::SoundFirmwareItem().check(context()).state, gw::CheckState::Done);
    }

    void intelHdaWithNoDriverIsNotNeeded() // class 0x0403: legacy HDA, not a DSP
    {
        device(QStringLiteral("0000:00:1f.3"), "0x8086", "0x040300", QString());
        QCOMPARE(gw::SoundFirmwareItem().check(context()).state, gw::CheckState::NotNeeded);
    }

    void applyInstallsTheFirmware()
    {
        const auto steps = gw::SoundFirmwareItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QCOMPARE(steps.size(), 1);
        QCOMPARE(steps[0].argv, QStringList({"zypper", "-n", "install", "sof-firmware"}));
        QVERIFY(gw::catalogue().find(QStringLiteral("sound-firmware")));
        QVERIFY(gw::catalogue().orderProblems().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstSoundFirmwareItem)
#include "tst_soundfirmwareitem.moc"
