// The codecs item: Packman repository detection from repos.d, the codec
// library's vendor from a fake rpm, and steps per system (ADR-0003).
#include "items/catalogue.h"
#include "items/codecsitem.h"

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
        repo(QStringLiteral("packman-essentials.repo"),
             "[packman-essentials]\nbaseurl=https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/Essentials/\n");
        fakeRpm(openSuseLibrary());
        QCOMPARE(gw::CodecsItem().check(context()).state, gw::CheckState::NotDone);
    }

    void disabledRepoDoesNotCount()
    {
        repo(QStringLiteral("packman.repo"),
             "[packman]\nenabled=0\nbaseurl=https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/\n");
        fakeRpm(packmanLibrary());
        QCOMPARE(gw::CodecsItem().check(context()).state, gw::CheckState::NotDone);
        QVERIFY(gw::CodecsItem::packmanAlias(context()).isEmpty());
    }

    void findsPackmanInAMultiSectionFile()
    {
        repo(QStringLiteral("mixed.repo"),
             "[oss]\nbaseurl=https://download.opensuse.org/tumbleweed/repo/oss/\n\n"
             "[pm]\nbaseurl=https://ftp.fau.de/packman/suse/openSUSE_Tumbleweed/Essentials/\n");
        QCOMPARE(gw::CodecsItem::packmanAlias(context()), QStringLiteral("pm"));
    }

    void failingRpmCouldNotTell()
    {
        fakeRpm("exit 1");
        QCOMPARE(gw::CodecsItem().check(context()).state, gw::CheckState::CouldNotTell);
    }

    void addsEssentialsForEachSystem_data()
    {
        QTest::addColumn<QByteArray>("osRelease");
        QTest::addColumn<QString>("url");
        QTest::newRow("tumbleweed") << QByteArray("ID=opensuse-tumbleweed\n")
            << "https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/Essentials/";
        QTest::newRow("slowroll") << QByteArray("ID=opensuse-slowroll\n")
            << "https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Slowroll/Essentials/";
        QTest::newRow("leap 16") << QByteArray("ID=opensuse-leap\nVERSION_ID=16.0\n")
            << "https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Leap_$releasever/Essentials/";
    }
    void addsEssentialsForEachSystem()
    {
        QFETCH(QByteArray, osRelease);
        QFETCH(QString, url);
        const auto steps = gw::CodecsItem().applySteps(gw::parseOsRelease(osRelease), context());
        QCOMPARE(steps.size(), 3);
        QCOMPARE(steps[0].argv, QStringList({"zypper", "-n", "addrepo", "-cfp", "90", url, "packman-essentials"}));
        QVERIFY(steps[2].argv.contains(QStringLiteral("--allow-vendor-change")));
        QCOMPARE(steps[2].argv.mid(4, 2), QStringList({"--from", "packman-essentials"}));
        for (const auto &s : steps)
            QVERIFY(s.needsRoot && s.tool == gw::Step::Tool::Zypper);
    }

    void reusesAnExistingPackmanRepository() // ADR-0003: never add Essentials beside it
    {
        repo(QStringLiteral("packman.repo"),
             "[packman]\nbaseurl=https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/\n");
        const auto steps = gw::CodecsItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QCOMPARE(steps.size(), 2);
        QCOMPARE(steps[0].argv.last(), QStringLiteral("packman"));
        QCOMPARE(steps[1].argv.mid(4, 2), QStringList({"--from", "packman"}));
    }

    void catalogueStaysOrdered() { QVERIFY(gw::catalogue().orderProblems().isEmpty()); }
};

QTEST_GUILESS_MAIN(TstCodecsItem)
#include "tst_codecsitem.moc"
