// The Broadcom Wi-Fi item: the chip list gate, the wl module as "done",
// the Secure Boot note, and the install route per system.
#include "items/broadcomitem.h"
#include "items/catalogue.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstBroadcomItem : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_root;

    void write(const QString &path, const QByteArray &data)
    {
        QVERIFY(QDir().mkpath(QFileInfo(m_root->path() + path).path()));
        QFile f(m_root->path() + path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }
    void card(const QByteArray &vendor, const QByteArray &device, const QByteArray &cls)
    {
        const QString dir = QStringLiteral("/sys/bus/pci/devices/0000:03:00.0/");
        write(dir + QStringLiteral("vendor"), vendor + '\n');
        write(dir + QStringLiteral("device"), device + '\n');
        write(dir + QStringLiteral("class"), cls + '\n');
    }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }

private slots:
    void init() { m_root = std::make_unique<QTemporaryDir>(); }

    void aChipWithNoOpenDriverIsNotDone()
    {
        card("0x14e4", "0x43a0", "0x028000");
        QCOMPARE(gw::BroadcomItem().check(context()).state, gw::CheckState::NotDone);
    }

    void aChipWithAnOpenDriverIsNotNeeded() // BCM43224: brcmsmac
    {
        card("0x14e4", "0x4353", "0x028000");
        QCOMPARE(gw::BroadcomItem().check(context()).state, gw::CheckState::NotNeeded);
    }

    void aBroadcomEthernetCardIsNotNeeded()
    {
        card("0x14e4", "0x4360", "0x020000"); // right id, wrong class
        QCOMPARE(gw::BroadcomItem().check(context()).state, gw::CheckState::NotNeeded);
    }

    void theLoadedModuleIsDone()
    {
        card("0x14e4", "0x4360", "0x028000");
        QVERIFY(QDir().mkpath(m_root->path() + QStringLiteral("/sys/module/wl")));
        QCOMPARE(gw::BroadcomItem().check(context()).state, gw::CheckState::Done);
    }

    void secureBootAddsTheKeyNote()
    {
        card("0x14e4", "0x4360", "0x028000");
        write(QStringLiteral("/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c"),
              QByteArray("\x06\x00\x00\x00\x01", 5));
        QVERIFY(gw::BroadcomItem().check(context()).detail.contains(QLatin1String("Secure Boot")));
        write(QStringLiteral("/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c"),
              QByteArray("\x06\x00\x00\x00\x00", 5));
        QVERIFY(!gw::BroadcomItem().check(context()).detail.contains(QLatin1String("Secure Boot")));
    }

    void tumbleweedInstallsFromItsOwnRepositories()
    {
        const auto steps = gw::BroadcomItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QCOMPARE(steps.size(), 1);
        QCOMPARE(steps[0].argv, QStringList({"zypper", "-n", "install", "broadcom-wl"}));
    }

    void leapInstallsFromPackmanEssentials()
    {
        const auto steps = gw::BroadcomItem().applySteps(gw::parseOsRelease("ID=opensuse-leap\nVERSION_ID=16.0\n"), context());
        QCOMPARE(steps.size(), 3); // add Essentials, refresh, install
        QCOMPARE(steps[0].argv.last(), QStringLiteral("packman-essentials"));
        QCOMPARE(steps[2].argv, QStringList({"zypper", "-n", "install", "--from", "packman-essentials", "broadcom-wl"}));
        QVERIFY(gw::catalogue().find(QStringLiteral("broadcom-wifi")));
        QVERIFY(gw::catalogue().orderProblems().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstBroadcomItem)
#include "tst_broadcomitem.moc"
