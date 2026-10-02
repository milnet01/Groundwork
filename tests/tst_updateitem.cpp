// The system update's rootless check and its per-system steps, against a
// fake zypper alone on PATH.
#include "items/catalogue.h"
#include "items/updateitem.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstUpdateItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin;
    QTemporaryDir m_root;
    QByteArray m_oldPath;

    void fakeZypper(const QByteArray &body)
    {
        QFile f(m_bin.filePath(QStringLiteral("zypper")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\n" + body + "\n");
        f.close();
        QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
    }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root.path())); }
    gw::CheckResult check() const { return gw::UpdateItem().check(context()); }

private slots:
    void initTestCase()
    {
        m_oldPath = qgetenv("PATH");
        qputenv("PATH", m_bin.path().toUtf8());
    }
    void cleanupTestCase() { qputenv("PATH", m_oldPath); }

    void neverDoneAndCountsWaitingUpdates()
    {
        fakeZypper("printf '<stream><update-list><update name=\"a\"/><update name=\"b\"/></update-list></stream>'");
        const auto r = check();
        QCOMPARE(r.state, gw::CheckState::NotDone);
        QVERIFY(r.detail.contains(QLatin1String("2")));
    }

    void noneWaitingIsStillNotDone()
    {
        fakeZypper("printf '<stream><update-list></update-list></stream>'");
        QCOMPARE(check().state, gw::CheckState::NotDone);
    }

    void skippedRepositoryDoesNotBlock() // exit 106 (design, Check results)
    {
        fakeZypper("printf '<stream><update-list><update name=\"a\"/></update-list></stream>'; exit 106");
        const auto r = check();
        QCOMPARE(r.state, gw::CheckState::NotDone);
        QVERIFY(r.detail.contains(QLatin1String("could not be read")));
    }

    void otherFailureCouldNotTell()
    {
        fakeZypper("exit 4");
        QCOMPARE(check().state, gw::CheckState::CouldNotTell);
    }

    void missingZypperCouldNotTell()
    {
        QFile::remove(m_bin.filePath(QStringLiteral("zypper")));
        QCOMPARE(check().state, gw::CheckState::CouldNotTell);
    }

    void rollingUsesDupAndLeapUsesUpdate()
    {
        const auto rolling = gw::UpdateItem().applySteps(gw::parseOsRelease("ID=opensuse-slowroll\n"), context());
        const auto leap = gw::UpdateItem().applySteps(gw::parseOsRelease("ID=opensuse-leap\nVERSION_ID=16.0\n"), context());
        QCOMPARE(rolling.last().argv, QStringList({"zypper", "-n", "dup"}));
        QCOMPARE(leap.last().argv, QStringList({"zypper", "-n", "update"}));
        QCOMPARE(rolling.first().argv, QStringList({"zypper", "-n", "refresh"}));
        for (const auto &s : rolling + leap) {
            QVERIFY(s.needsRoot);
            QCOMPARE(s.tool, gw::Step::Tool::Zypper);
        }
    }

    void isTheFirstPreparationInTheCatalogue()
    {
        const auto &all = gw::catalogue();
        QCOMPARE(all.items().first()->id(), QStringLiteral("system-update"));
        QVERIFY(all.items().first()->isPreparation());
        QVERIFY(all.orderProblems().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstUpdateItem)
#include "tst_updateitem.moc"
