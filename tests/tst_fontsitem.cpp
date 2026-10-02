// The Microsoft fonts item against fake fc-list and zypper.
#include "items/catalogue.h"
#include "items/fontsitem.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstFontsItem : public QObject
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

    void arialPresentIsDone()
    {
        fake(QStringLiteral("fc-list"), "printf 'DejaVu Sans\\nArial\\n'");
        QCOMPARE(gw::FontsItem().check(context()).state, gw::CheckState::Done);
    }

    void aFontNamedLikeArialDoesNotCount()
    {
        fake(QStringLiteral("fc-list"), "printf 'Arial Unicode Lookalike\\n'");
        fake(QStringLiteral("zypper"), "exit 0");
        QCOMPARE(gw::FontsItem().check(context()).state, gw::CheckState::NotDone);
    }

    void offeredButMissingIsNotDone() // the author's Tumbleweed
    {
        fake(QStringLiteral("fc-list"), "printf 'DejaVu Sans\\n'");
        fake(QStringLiteral("zypper"), "exit 0");
        QCOMPARE(gw::FontsItem().check(context()).state, gw::CheckState::NotDone);
        const auto steps = gw::FontsItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QCOMPARE(steps[0].argv, QStringList({"zypper", "-n", "install", "fetchmsttfonts"}));
    }

    void notOfferedIsNotNeeded() // Leap 16.0: zypper exits 104
    {
        fake(QStringLiteral("fc-list"), "printf 'DejaVu Sans\\n'");
        fake(QStringLiteral("zypper"), "echo \"package 'fetchmsttfonts' not found.\"; exit 104");
        QCOMPARE(gw::FontsItem().check(context()).state, gw::CheckState::NotNeeded);
        QVERIFY(gw::catalogue().find(QStringLiteral("microsoft-fonts")));
        QVERIFY(gw::catalogue().orderProblems().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstFontsItem)
#include "tst_fontsitem.moc"
