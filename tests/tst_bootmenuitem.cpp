// A shorter wait at the boot menu (GRND-0057), against a fixture root
// holding the boot loader's settings and a fake sdbootutil first on PATH.
#include "items/bootmenuitem.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

namespace {
const QString kTimeoutVar =
    QStringLiteral("sys/firmware/efi/efivars/LoaderConfigTimeout-4a67b082-0a4c-41cf-b6c7-440b29bb8c4f");

// efivarfs: 4 bytes of attributes, then the value in UTF-16LE with a NUL.
QByteArray efiString(const QString &value)
{
    QByteArray data("\x07\0\0\0", 4);
    for (const QChar c : value + QChar(0)) {
        data += char(c.unicode() & 0xff);
        data += char(c.unicode() >> 8);
    }
    return data;
}
} // namespace

class TstBootMenuItem : public QObject
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
    void rooted(const QString &path, const QByteArray &text) { write(m_root->filePath(path), text); }
    void loader(const QByteArray &type) { rooted(QStringLiteral("etc/sysconfig/bootloader"), "LOADER_TYPE=\"" + type + "\"\n"); }
    void sdbootutil(const QByteArray &script)
    {
        write(m_bin.filePath(QStringLiteral("sdbootutil")), "#!/bin/sh\n" + script + "\n", true);
    }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }
    gw::CheckState state() const { return gw::BootMenuItem().check(context()).state; }

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
        QFile::remove(m_bin.filePath(QStringLiteral("sdbootutil")));
    }

    void notNeededWithoutABootLoaderItKnows()
    {
        QCOMPARE(state(), gw::CheckState::NotNeeded);
        loader("none");
        QCOMPARE(state(), gw::CheckState::NotNeeded);
    }

    // sdbootutil's loaders: the firmware variable decides, and a hidden
    // menu counts as short.
    void theFirmwareVariableDecides_data()
    {
        QTest::addColumn<QByteArray>("type");
        QTest::addColumn<QString>("value");
        QTest::addColumn<gw::CheckState>("expected");
        QTest::newRow("grub2-bls, 8") << QByteArray("grub2-bls") << QStringLiteral("8") << gw::CheckState::NotDone;
        QTest::newRow("grub2-bls, 3") << QByteArray("grub2-bls") << QStringLiteral("3") << gw::CheckState::Done;
        QTest::newRow("systemd-boot, 1") << QByteArray("systemd-boot") << QStringLiteral("1") << gw::CheckState::Done;
        QTest::newRow("menu-hidden") << QByteArray("grub2-bls") << QStringLiteral("menu-hidden") << gw::CheckState::Done;
        QTest::newRow("menu-force") << QByteArray("systemd-boot") << QStringLiteral("menu-force")
                                    << gw::CheckState::NotDone;
    }
    void theFirmwareVariableDecides()
    {
        QFETCH(QByteArray, type);
        QFETCH(QString, value);
        QFETCH(gw::CheckState, expected);
        loader(type);
        rooted(kTimeoutVar, efiString(value));
        sdbootutil("echo 0"); // never asked while the variable answers
        QCOMPARE(state(), expected);
    }

    // Without the variable the wait is the one installed on the root-only
    // EFI partition, which neither the wizard nor the Worker can read: both
    // run as the user. It is not done, so applying can set the variable.
    void withoutTheVariableItIsNotDone()
    {
        loader("grub2-bls");
        sdbootutil("[ \"$1\" = get-timeout ] && echo 2"); // never asked
        QCOMPARE(state(), gw::CheckState::NotDone);
        loader("systemd-boot");
        QCOMPARE(state(), gw::CheckState::NotDone);
    }

    // Plain grub2 waits 5 seconds when /etc/default/grub does not say.
    void grubReadsItsDefaults()
    {
        loader("grub2-efi");
        rooted(QStringLiteral("etc/default/grub"), "GRUB_DISTRIBUTOR=x\nGRUB_TIMEOUT=8\n");
        QCOMPARE(state(), gw::CheckState::NotDone);
        rooted(QStringLiteral("etc/default/grub"), "GRUB_TIMEOUT=\"3\"\n");
        QCOMPARE(state(), gw::CheckState::Done);
        loader("grub2");
        rooted(QStringLiteral("etc/default/grub"), "GRUB_DISTRIBUTOR=x\n");
        QCOMPARE(state(), gw::CheckState::NotDone);
    }

    void applyingWithSdbootutil()
    {
        loader("systemd-boot");
        const QList<gw::Step> steps = gw::BootMenuItem().applySteps({}, context());
        QCOMPARE(steps.size(), 1);
        QCOMPARE(steps[0].argv, (QStringList{QStringLiteral("sdbootutil"), QStringLiteral("set-timeout"),
                                             QStringLiteral("3")}));
        QVERIFY(steps[0].needsRoot);
    }

    // The line is replaced where there is one and added where there is not;
    // then grub's menu is rebuilt from it.
    void applyingWithGrub()
    {
        loader("grub2");
        const QStringList rebuild = {QStringLiteral("grub2-mkconfig"), QStringLiteral("-o"),
                                     QStringLiteral("/boot/grub2/grub.cfg")};
        rooted(QStringLiteral("etc/default/grub"), "GRUB_TIMEOUT=8\n");
        QList<gw::Step> steps = gw::BootMenuItem().applySteps({}, context());
        QCOMPARE(steps.size(), 2);
        QCOMPARE(steps[0].argv.mid(0, 4), (QStringList{QStringLiteral("sed"), QStringLiteral("-i"), QStringLiteral("-E"),
                                                       QStringLiteral("s/^[[:space:]]*GRUB_TIMEOUT=.*/GRUB_TIMEOUT=3/")}));
        QCOMPARE(steps[0].argv.last(), QStringLiteral("/etc/default/grub"));
        QCOMPARE(steps[1].argv, rebuild);

        rooted(QStringLiteral("etc/default/grub"), "GRUB_DISTRIBUTOR=x\n");
        steps = gw::BootMenuItem().applySteps({}, context());
        QCOMPARE(steps.size(), 2);
        QCOMPARE(steps[0].argv, (QStringList{QStringLiteral("sed"), QStringLiteral("-i"),
                                             QStringLiteral("$aGRUB_TIMEOUT=3"), QStringLiteral("/etc/default/grub")}));
        QCOMPARE(steps[1].argv, rebuild);
        for (const gw::Step &s : steps)
            QVERIFY(s.needsRoot);
    }

    // GRND-0093: the check reads an indented GRUB_TIMEOUT line, so the
    // edit changes one too; real sed, on a copy of the file.
    void anIndentedTimeoutIsChanged()
    {
        loader("grub2");
        rooted(QStringLiteral("etc/default/grub"), "GRUB_DISTRIBUTOR=x\n  GRUB_TIMEOUT=8\n");
        QStringList argv = gw::BootMenuItem().applySteps({}, context()).value(0).argv;
        QVERIFY(!argv.isEmpty());
        argv.last() = m_root->filePath(QStringLiteral("etc/default/grub"));
        QCOMPARE(argv.first(), QStringLiteral("sed"));
        QCOMPARE(QProcess::execute(QStringLiteral("/usr/bin/sed"), argv.mid(1)), 0); // PATH holds only fakes
        QFile f(argv.last());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        QVERIFY2(text.contains("GRUB_TIMEOUT=3") && !text.contains("GRUB_TIMEOUT=8"), text.constData());
    }
};

QTEST_MAIN(TstBootMenuItem)
#include "tst_bootmenuitem.moc"
