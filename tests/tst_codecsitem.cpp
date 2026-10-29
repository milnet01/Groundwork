// The codecs item: Packman repository detection from repos.d, the codec
// library's vendor from a fake rpm, and steps per system (ADR-0004).
#include "items/catalogue.h"
#include "items/codecsitem.h"
#include "items/packman.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstCodecsItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin;
    QByteArray m_oldPath;
    std::unique_ptr<QTemporaryDir> m_root;

    void fakeRpm(const QByteArray &body)
    {
        QFile f(m_bin.filePath(QStringLiteral("rpm")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\n" + body + "\n");
        f.close();
        QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
    }
    void repo(const QString &file, const QByteArray &text)
    {
        QVERIFY(QDir(m_root->path()).mkpath(QStringLiteral("etc/zypp/repos.d")));
        QFile f(m_root->path() + QStringLiteral("/etc/zypp/repos.d/") + file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text);
    }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }

    static QByteArray packmanLibrary() { return "printf 'libavcodec62\\thttp://packman.links2linux.de\\n'"; }
    static QByteArray openSuseLibrary() { return "printf 'libavcodec62\\topenSUSE\\n'"; }

private slots:
    void initTestCase()
    {
        m_oldPath = qgetenv("PATH");
        qputenv("PATH", m_bin.path().toUtf8());
    }
    void cleanupTestCase() { qputenv("PATH", m_oldPath); }
    void init() { m_root = std::make_unique<QTemporaryDir>(); }

    void freshSystemIsNotDone()
    {
        fakeRpm(openSuseLibrary());
        QCOMPARE(gw::CodecsItem().check(context()).state, gw::CheckState::NotDone);
    }

    void fullPackmanWithItsLibraryIsDone() // the author's machine
    {
        repo(QStringLiteral("packman.repo"),
             "[packman]\nenabled=1\nbaseurl=https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/\npriority=90\n");
        fakeRpm(packmanLibrary());
        QCOMPARE(gw::CodecsItem().check(context()).state, gw::CheckState::Done);
    }

    void repoWithoutItsLibraryIsNotDone()
    {
        repo(QStringLiteral("packman.repo"),
             "[packman]\nbaseurl=https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/\n");
        fakeRpm(openSuseLibrary());
        QCOMPARE(gw::CodecsItem().check(context()).state, gw::CheckState::NotDone);
    }

    // A system set up under ADR-0003: Essentials only, its codecs Packman's.
    void essentialsOnlyIsNotDone()
    {
        repo(QStringLiteral("packman-essentials.repo"),
             "[packman-essentials]\nbaseurl=https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/Essentials/\n");
        fakeRpm(packmanLibrary());
        QCOMPARE(gw::CodecsItem().check(context()).state, gw::CheckState::NotDone);
        QVERIFY(gw::packmanAlias(context()).isEmpty());
    }

    void disabledRepoDoesNotCount()
    {
        repo(QStringLiteral("packman.repo"),
             "[packman]\nenabled=0\nbaseurl=https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/\n");
        fakeRpm(packmanLibrary());
        QCOMPARE(gw::CodecsItem().check(context()).state, gw::CheckState::NotDone);
        QVERIFY(gw::packmanAlias(context()).isEmpty());
    }

    // GRND-0093: a Packman the user switched off is switched on to
    // install from, never added again under its alias.
    void aDisabledPackmanIsSwitchedOn()
    {
        repo(QStringLiteral("packman.repo"),
             "[packman]\nenabled=0\nbaseurl=https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/\n");
        const auto steps = gw::CodecsItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QVERIFY(!steps.isEmpty());
        QCOMPARE(steps[0].argv, QStringList({"zypper", "-n", "modifyrepo", "-e", "packman"}));
        for (const auto &step : steps)
            QVERIFY2(!step.argv.contains(QStringLiteral("addrepo")), qPrintable(step.argv.join(QLatin1Char(' '))));
    }

    void findsPackmanInAMultiSectionFile()
    {
        repo(QStringLiteral("mixed.repo"),
             "[oss]\nbaseurl=https://download.opensuse.org/tumbleweed/repo/oss/\n\n"
             "[pm-ess]\nbaseurl=https://ftp.fau.de/packman/suse/openSUSE_Tumbleweed/Essentials/\n\n"
             "[pm]\nbaseurl=https://ftp.fau.de/packman/suse/openSUSE_Tumbleweed/\n");
        QCOMPARE(gw::packmanAlias(context()), QStringLiteral("pm"));
    }

    void failingRpmCouldNotTell()
    {
        fakeRpm("exit 1");
        QCOMPARE(gw::CodecsItem().check(context()).state, gw::CheckState::CouldNotTell);
    }

    void addsFullPackmanForEachSystem_data()
    {
        QTest::addColumn<QByteArray>("osRelease");
        QTest::addColumn<QString>("url");
        QTest::newRow("tumbleweed") << QByteArray("ID=opensuse-tumbleweed\n")
            << "https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/";
        QTest::newRow("slowroll") << QByteArray("ID=opensuse-slowroll\n")
            << "https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Slowroll/";
        QTest::newRow("leap 16") << QByteArray("ID=opensuse-leap\nVERSION_ID=16.0\n")
            << "https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Leap_$releasever/";
    }
    void addsFullPackmanForEachSystem()
    {
        QFETCH(QByteArray, osRelease);
        QFETCH(QString, url);
        const auto steps = gw::CodecsItem().applySteps(gw::parseOsRelease(osRelease), context());
        QCOMPARE(steps.size(), 4); // add, refresh, switch to Packman's builds, install
        QCOMPARE(steps[0].argv, QStringList({"zypper", "-n", "addrepo", "-cfp", "90", url, "packman"}));
        QCOMPARE(steps[2].argv, QStringList({"zypper", "-n", "dup", "--from", "packman", "--allow-downgrade",
                                             "--allow-vendor-change"}));
        QVERIFY(steps[3].argv.contains(QStringLiteral("--allow-vendor-change")));
        QCOMPARE(steps[3].argv.mid(4, 2), QStringList({"--from", "packman"}));
        for (const auto &s : steps)
            QVERIFY(s.needsRoot && s.tool == gw::Step::Tool::Zypper);
    }

    void reusesAnExistingPackmanRepository()
    {
        repo(QStringLiteral("pm.repo"),
             "[pm]\nbaseurl=https://ftp.fau.de/packman/suse/openSUSE_Tumbleweed/\n");
        const auto steps = gw::CodecsItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QCOMPARE(steps.size(), 3);
        QCOMPARE(steps[0].argv.last(), QStringLiteral("pm"));
        QCOMPARE(steps[1].argv.mid(3, 2), QStringList({"--from", "pm"}));
        QCOMPARE(steps[2].argv.mid(4, 2), QStringList({"--from", "pm"}));
    }

    // ADR-0004: Essentials is left in place, and the full repository added.
    void addsFullPackmanBesideEssentials()
    {
        repo(QStringLiteral("packman-essentials.repo"),
             "[packman-essentials]\nbaseurl=https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/Essentials/\n");
        const auto steps = gw::CodecsItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QCOMPARE(steps.size(), 4);
        QCOMPARE(steps[0].argv.last(), QStringLiteral("packman"));
        QCOMPARE(steps[3].argv.mid(4, 2), QStringList({"--from", "packman"}));
    }

    void catalogueStaysOrdered() { QVERIFY(gw::catalogue().orderProblems().isEmpty()); }
};

QTEST_GUILESS_MAIN(TstCodecsItem)
#include "tst_codecsitem.moc"
