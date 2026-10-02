// Which systems are supported, decided from os-release ID and VERSION_ID.
// The IDs are those in docs/research/2026-10-02-sources.md.
#include "core/systemidentity.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using Family = gw::SystemIdentity::Family;
Q_DECLARE_METATYPE(gw::SystemIdentity::Family)

class TstSystemIdentity : public QObject
{
    Q_OBJECT

    static void write(const QTemporaryDir &root, const QString &path, const QByteArray &text)
    {
        QVERIFY(QDir(root.path()).mkpath(QFileInfo(root.path() + path).path()));
        QFile f(root.path() + path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(text);
    }

private slots:
    void family_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<Family>("family");
        QTest::addColumn<QString>("id");

        QTest::newRow("tumbleweed")
            << QByteArray("NAME=\"openSUSE Tumbleweed\"\nID=\"opensuse-tumbleweed\"\nVERSION_ID=\"20260930\"\n")
            << Family::Rolling << "opensuse-tumbleweed";
        QTest::newRow("slowroll")
            << QByteArray("ID=opensuse-slowroll\nID_LIKE=\"opensuse-tumbleweed opensuse suse\"\n")
            << Family::Rolling << "opensuse-slowroll";
        QTest::newRow("leap 16.0")
            << QByteArray("ID=\"opensuse-leap\"\nVERSION_ID=\"16.0\"\n") << Family::Leap << "opensuse-leap";
        QTest::newRow("leap 17.1")
            << QByteArray("ID=opensuse-leap\nVERSION_ID=17.1\n") << Family::Leap << "opensuse-leap";
        QTest::newRow("leap 15.6 unsupported")
            << QByteArray("ID=\"opensuse-leap\"\nVERSION_ID=\"15.6\"\n") << Family::Unsupported << "opensuse-leap";
        QTest::newRow("leap micro unsupported, despite ID_LIKE")
            << QByteArray("ID=\"opensuse-leap-micro\"\nID_LIKE=\"suse opensuse opensuse-leap suse-microos\"\n")
            << Family::Unsupported << "opensuse-leap-micro";
        QTest::newRow("another distribution")
            << QByteArray("ID=fedora\nPRETTY_NAME=\"Fedora Linux 44\"\n") << Family::Unsupported << "fedora";
        QTest::newRow("single quotes and comments")
            << QByteArray("# ID=fedora\n\nID='opensuse-tumbleweed'\n") << Family::Rolling << "opensuse-tumbleweed";
        QTest::newRow("no ID") << QByteArray("NAME=Linux\n") << Family::Unsupported << "";
        QTest::newRow("leap with no number")
            << QByteArray("ID=opensuse-leap\nVERSION_ID=\"rolling\"\n") << Family::Unsupported << "opensuse-leap";
    }

    void family()
    {
        QFETCH(QByteArray, text);
        QFETCH(Family, family);
        QFETCH(QString, id);
        const auto identity = gw::parseOsRelease(text);
        QCOMPARE(identity.family, family);
        QCOMPARE(identity.id, id);
        // An unsupported system always says why, in plain words.
        QCOMPARE(identity.reason.isEmpty(), identity.supported());
    }

    void unescapesDoubleQuotes()
    {
        const auto identity = gw::parseOsRelease("ID=fedora\nPRETTY_NAME=\"A \\\"quoted\\\" name\"\n");
        QCOMPARE(identity.prettyName, QStringLiteral("A \"quoted\" name"));
    }

    void prefersEtcAndNeverCombines()
    {
        QTemporaryDir root;
        write(root, QStringLiteral("/etc/os-release"), "ID=opensuse-tumbleweed\n");
        write(root, QStringLiteral("/usr/lib/os-release"), "ID=fedora\nVERSION_ID=44\n");
        const auto identity = gw::readSystemIdentity(gw::FileReader(root.path()));
        QCOMPARE(identity.id, QStringLiteral("opensuse-tumbleweed"));
        QVERIFY(identity.versionId.isEmpty());
    }

    void fallsBackToUsrLib()
    {
        QTemporaryDir root;
        write(root, QStringLiteral("/usr/lib/os-release"), "ID=opensuse-leap\nVERSION_ID=16.0\n");
        QCOMPARE(gw::readSystemIdentity(gw::FileReader(root.path())).family, Family::Leap);
    }

    void missingFileIsUnsupportedWithReason()
    {
        QTemporaryDir root;
        const auto identity = gw::readSystemIdentity(gw::FileReader(root.path()));
        QVERIFY(!identity.supported());
        QVERIFY(!identity.reason.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstSystemIdentity)
#include "tst_systemidentity.moc"
