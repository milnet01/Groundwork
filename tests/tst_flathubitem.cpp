// The Flathub item's check, against a fake flatpak first on PATH, and the
// catalogue's order.
#include "items/catalogue.h"
#include "items/flathubitem.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstFlathubItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin;
    QTemporaryDir m_root;
    QByteArray m_oldPath;

    void fakeFlatpak(const QByteArray &body)
    {
        QFile f(m_bin.filePath(QStringLiteral("flatpak")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\n" + body + "\n");
        f.close();
        QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
    }
    gw::CheckResult check() const
    {
        return gw::FlathubItem().check(gw::CheckContext(gw::FileReader(m_root.path())));
    }

private slots:
    void initTestCase()
    {
        m_oldPath = qgetenv("PATH");
        // The fake directory alone, so a real flatpak can never answer.
        qputenv("PATH", m_bin.path().toUtf8());
    }
    void cleanupTestCase() { qputenv("PATH", m_oldPath); }

    void systemRemoteIsDone()
    {
        fakeFlatpak("printf 'fedora\\tsystem\\nflathub\\tsystem\\n'");
        QCOMPARE(check().state, gw::CheckState::Done);
    }

    void userRemoteIsDone()
    {
        fakeFlatpak("printf 'flathub\\tuser\\n'");
        QCOMPARE(check().state, gw::CheckState::Done);
    }

    void otherRemotesOnlyIsNotDone()
    {
        fakeFlatpak("printf 'flathub-beta\\tsystem\\nfedora\\tsystem\\n'");
        QCOMPARE(check().state, gw::CheckState::NotDone);
    }

    void noFlatpakIsNotDone()
    {
        QFile::remove(m_bin.filePath(QStringLiteral("flatpak")));
        QCOMPARE(check().state, gw::CheckState::NotDone);
    }

    void failingFlatpakCouldNotTell()
    {
        fakeFlatpak("exit 1");
        const auto r = check();
        QCOMPARE(r.state, gw::CheckState::CouldNotTell);
        QVERIFY(!r.detail.isEmpty());
    }

    void applyInstallsThenAddsTheSystemRemote()
    {
        const auto steps = gw::FlathubItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"));
        QCOMPARE(steps.size(), 2);
        QCOMPARE(steps[0].tool, gw::Step::Tool::Zypper);
        QVERIFY(steps[0].needsRoot && steps[1].needsRoot);
        QCOMPARE(steps[1].argv.last(), QStringLiteral("https://dl.flathub.org/repo/flathub.flatpakrepo"));
        // Bare names, so the fakes on PATH replace them in tests.
        for (const auto &s : steps)
            QVERIFY(!s.argv.first().contains(QLatin1Char('/')));
    }

    void catalogueIsWellOrdered() { QVERIFY(gw::catalogue().orderProblems().isEmpty()); }
};

QTEST_GUILESS_MAIN(TstFlathubItem)
#include "tst_flathubitem.moc"
