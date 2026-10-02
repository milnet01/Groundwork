// The computer-name item: which names are valid, its states against a
// fake hostnamectl, and the value placeholder in its step.
#include "items/catalogue.h"
#include "items/hostnameitem.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstHostnameItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin, m_root;
    QByteArray m_oldPath;

    void name(const QByteArray &printed)
    {
        QFile f(m_bin.filePath(QStringLiteral("hostnamectl")));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("#!/bin/sh\necho '" + printed + "'\n");
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

    void validNames_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<bool>("valid");
        QTest::newRow("plain") << "AntsPC" << true;
        QTest::newRow("hyphen inside") << "lounge-pc-2" << true;
        QTest::newRow("one character") << "a" << true;
        QTest::newRow("63 characters") << QString(63, QLatin1Char('a')) << true;
        QTest::newRow("64 characters") << QString(64, QLatin1Char('a')) << false;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("leading hyphen") << "-pc" << false;
        QTest::newRow("trailing hyphen") << "pc-" << false;
        QTest::newRow("space") << "my pc" << false;
        QTest::newRow("dot") << "pc.home" << false;
        QTest::newRow("shell text") << "$(reboot)" << false;
    }
    void validNames()
    {
        QFETCH(QString, value);
        QFETCH(bool, valid);
        QCOMPARE(gw::HostnameItem().isValidValue(value), valid);
    }

    void states()
    {
        name("AntsPC");
        QCOMPARE(gw::HostnameItem().check(context()).state, gw::CheckState::Done);
        name("localhost");
        QCOMPARE(gw::HostnameItem().check(context()).state, gw::CheckState::NotDone);
        name("localhost.localdomain");
        QCOMPARE(gw::HostnameItem().check(context()).state, gw::CheckState::NotDone);
    }

    void stepCarriesTheValuePlaceholder()
    {
        const auto steps = gw::HostnameItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
        QCOMPARE(steps.size(), 1);
        QCOMPARE(steps[0].argv, QStringList({"hostnamectl", "hostname", gw::kValuePlaceholder}));
        QVERIFY(!gw::HostnameItem().valuePrompt().isEmpty());
        QVERIFY(gw::catalogue().find(QStringLiteral("computer-name")));
        QVERIFY(gw::catalogue().orderProblems().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstHostnameItem)
#include "tst_hostnameitem.moc"
