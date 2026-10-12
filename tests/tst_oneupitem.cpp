// OneUp (GRND-0074), against fixture os-release and repos.d trees and a
// fake rpm first on PATH that knows the packages listed in FAKE_INSTALLED.
#include "items/catalogue.h"
#include "items/oneupitem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstOneUpItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin;
    std::unique_ptr<QTemporaryDir> m_root;
    QByteArray m_oldPath;

    void write(const QString &path, const QByteArray &data)
    {
        QVERIFY(QDir().mkpath(QFileInfo(m_root->path() + path).path()));
        QFile f(m_root->path() + path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(data);
    }
    void system(const QByteArray &osRelease) { write(QStringLiteral("/etc/os-release"), osRelease); }
    void installed(const QByteArray &packages) { qputenv("FAKE_INSTALLED", " " + packages + " "); }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }
    gw::CheckState state() const { return gw::OneUpItem().check(context()).state; }

private slots:
    void initTestCase()
    {
        QFile f(m_bin.filePath(QStringLiteral("rpm")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\ncase \"$FAKE_INSTALLED\" in *\" $2 \"*) exit 0;; esac\nexit 1\n");
        f.close();
        QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
        m_oldPath = qgetenv("PATH");
        qputenv("PATH", m_bin.path().toUtf8());
    }
    void cleanupTestCase() { qputenv("PATH", m_oldPath); }
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        installed("");
    }

    // The repository builds for Tumbleweed and Leap 16.0 only (checked
    // 2026-10-12: Slowroll and Leap 16.1 answer 404).
    void notNeededWhereOneUpIsNotBuilt_data()
    {
        QTest::addColumn<QByteArray>("osRelease");
        QTest::newRow("Slowroll") << QByteArray("ID=opensuse-slowroll\n");
        QTest::newRow("Leap 16.1") << QByteArray("ID=opensuse-leap\nVERSION_ID=\"16.1\"\n");
    }
    void notNeededWhereOneUpIsNotBuilt()
    {
        QFETCH(QByteArray, osRelease);
        system(osRelease);
        QCOMPARE(state(), gw::CheckState::NotNeeded);
    }

    void doneOnceInstalled_data()
    {
        QTest::addColumn<QByteArray>("osRelease");
        QTest::newRow("Tumbleweed") << QByteArray("ID=opensuse-tumbleweed\n");
        QTest::newRow("Leap 16.0") << QByteArray("ID=opensuse-leap\nVERSION_ID=\"16.0\"\n");
    }
    void doneOnceInstalled()
    {
        QFETCH(QByteArray, osRelease);
        system(osRelease);
        QCOMPARE(state(), gw::CheckState::NotDone);
        installed("oneup");
        QCOMPARE(state(), gw::CheckState::Done);
    }

    // Beside openSUSE's own updates: this item installs OneUp and touches
    // nothing else.
    void addsTheRepositoryAndInstalls_data()
    {
        QTest::addColumn<QByteArray>("osRelease");
        QTest::addColumn<QString>("url");
        QTest::newRow("Tumbleweed") << QByteArray("ID=opensuse-tumbleweed\n")
            << "https://download.opensuse.org/repositories/home:/milnet/openSUSE_Tumbleweed/";
        QTest::newRow("Leap 16.0") << QByteArray("ID=opensuse-leap\nVERSION_ID=\"16.0\"\n")
            << "https://download.opensuse.org/repositories/home:/milnet/openSUSE_Leap_16.0/";
    }
    void addsTheRepositoryAndInstalls()
    {
        QFETCH(QByteArray, osRelease);
        QFETCH(QString, url);
        // Another of the author's repositories is not this one.
        write(QStringLiteral("/etc/zypp/repos.d/finbreak.repo"),
              "[finbreak]\nbaseurl=https://download.opensuse.org/repositories/home:/milnet:/finbreak/openSUSE_Tumbleweed/\n");
        const QList<gw::Step> steps = gw::OneUpItem().applySteps(gw::parseOsRelease(osRelease), context());
        QCOMPARE(steps.size(), 3);
        QCOMPARE(steps[0].argv, (QStringList{"zypper", "-n", "addrepo", "-f", url, "home_milnet"}));
        QCOMPARE(steps[1].argv, (QStringList{"zypper", "-n", "--gpg-auto-import-keys", "refresh", "home_milnet"}));
        QCOMPARE(steps[2].argv, (QStringList{"zypper", "-n", "install", "oneup"}));
        for (const gw::Step &s : steps)
            QVERIFY(s.needsRoot && s.tool == gw::Step::Tool::Zypper);
    }

    void reusesTheRepositoryAlreadyAdded()
    {
        write(QStringLiteral("/etc/zypp/repos.d/milnet.repo"),
              "[my-milnet]\nbaseurl=https://download.opensuse.org/repositories/home:/milnet/openSUSE_Tumbleweed/\n");
        const QList<gw::Step> steps = gw::OneUpItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QCOMPARE(steps.size(), 2);
        QCOMPARE(steps[0].argv.last(), QStringLiteral("my-milnet"));
        QVERIFY(gw::catalogue().find(QStringLiteral("oneup")));
        QVERIFY(gw::catalogue().orderProblems().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstOneUpItem)
#include "tst_oneupitem.moc"
