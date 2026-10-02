// The snapshots item against fake findmnt and snapper.
#include "items/catalogue.h"
#include "items/snapshotsitem.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstSnapshotsItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin, m_root;
    QByteArray m_oldPath;

    void fake(const QString &name, const QByteArray &body)
    {
        QFile f(m_bin.filePath(name));
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

    void configuredRootIsDone() // the author's machine
    {
        fake(QStringLiteral("findmnt"), "echo btrfs");
        fake(QStringLiteral("snapper"), "printf 'config,subvolume\\nroot,/\\n'");
        QCOMPARE(gw::SnapshotsItem().check(context()).state, gw::CheckState::Done);
    }

    void onlyAHomeConfigIsNotDone()
    {
        fake(QStringLiteral("findmnt"), "echo btrfs");
        fake(QStringLiteral("snapper"), "printf 'config,subvolume\\nhome,/home\\n'");
        QCOMPARE(gw::SnapshotsItem().check(context()).state, gw::CheckState::NotDone);
        const auto steps = gw::SnapshotsItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QCOMPARE(steps.size(), 1);
        QCOMPARE(steps[0].argv, QStringList({"snapper", "--config", "root", "create-config", "/"}));
    }

    void notBtrfsIsNotNeeded()
    {
        fake(QStringLiteral("findmnt"), "echo ext4");
        QCOMPARE(gw::SnapshotsItem().check(context()).state, gw::CheckState::NotNeeded);
    }

    void missingSnapperIsInstalledFirst()
    {
        fake(QStringLiteral("findmnt"), "echo btrfs");
        QFile::remove(m_bin.filePath(QStringLiteral("snapper")));
        QCOMPARE(gw::SnapshotsItem().check(context()).state, gw::CheckState::NotDone);
        const auto steps = gw::SnapshotsItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QCOMPARE(steps.size(), 2);
        QCOMPARE(steps[0].argv, QStringList({"zypper", "-n", "install", "snapper", "snapper-zypp-plugin"}));
        QVERIFY(gw::catalogue().find(QStringLiteral("snapshots")));
        QVERIFY(gw::catalogue().orderProblems().isEmpty());
    }

    void unreadableFileSystemCouldNotTell()
    {
        fake(QStringLiteral("findmnt"), "exit 1");
        QCOMPARE(gw::SnapshotsItem().check(context()).state, gw::CheckState::CouldNotTell);
    }
};

QTEST_GUILESS_MAIN(TstSnapshotsItem)
#include "tst_snapshotsitem.moc"
