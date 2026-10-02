// FileReader resolves every path under its root, so a test tree stands in for
// the real system.
#include "core/filereader.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstFileReader : public QObject
{
    Q_OBJECT
private slots:
    void readsUnderRoot()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(QDir(root.path()).mkpath(QStringLiteral("etc")));
        QFile f(root.filePath(QStringLiteral("etc/os-release")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("ID=opensuse-tumbleweed\n");
        f.close();

        const gw::FileReader reader(root.path());
        QCOMPARE(reader.read(QStringLiteral("/etc/os-release")).value(),
                 QByteArray("ID=opensuse-tumbleweed\n"));
        QVERIFY(reader.exists(QStringLiteral("/etc/os-release")));
    }

    void absentFileIsNothing()
    {
        QTemporaryDir root;
        const gw::FileReader reader(root.path());
        QVERIFY(!reader.read(QStringLiteral("/etc/os-release")).has_value());
        QVERIFY(!reader.exists(QStringLiteral("/etc/os-release")));
    }

    void cannotEscapeRoot()
    {
        // "/../" must not climb out of the test tree into the real system.
        QTemporaryDir root;
        const gw::FileReader reader(root.path());
        QVERIFY(!reader.read(QStringLiteral("/../../../../etc/hostname")).has_value());
    }

    void listsEntriesSorted()
    {
        QTemporaryDir root;
        QDir d(root.path());
        QVERIFY(d.mkpath(QStringLiteral("sys/devices/b")));
        QVERIFY(d.mkpath(QStringLiteral("sys/devices/a")));
        const gw::FileReader reader(root.path());
        QCOMPARE(reader.entries(QStringLiteral("/sys/devices")),
                 QStringList({QStringLiteral("a"), QStringLiteral("b")}));
        QVERIFY(reader.entries(QStringLiteral("/nowhere")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstFileReader)
#include "tst_filereader.moc"
