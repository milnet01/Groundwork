// The system log capped at 1 GB (GRND-0056), against a fixture root holding
// journald's configuration files.
#include "items/logcapitem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstLogCapItem : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_root;

    void write(const QString &path, const QByteArray &text)
    {
        const QString full = m_root->filePath(path);
        QVERIFY(QDir().mkpath(QFileInfo(full).path()));
        QFile f(full);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
    }
    void cap(const QString &path, const QByteArray &size) { write(path, "[Journal]\nSystemMaxUse=" + size + "\n"); }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }
    gw::CheckState state() const { return gw::LogCapItem().check(context()).state; }

private slots:
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        write(QStringLiteral("usr/lib/systemd/journald.conf"), "[Journal]\n#SystemMaxUse=\n");
    }

    void notDoneWithoutACap() { QCOMPARE(state(), gw::CheckState::NotDone); }

    void doneWithACapOfAtMostOneGigabyte_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QByteArray>("size");
        QTest::newRow("drop-in, 500M") << QStringLiteral("etc/systemd/journald.conf.d/50-mine.conf") << QByteArray("500M");
        QTest::newRow("drop-in, 1G") << QStringLiteral("usr/lib/systemd/journald.conf.d/50-mine.conf") << QByteArray("1G");
        QTest::newRow("main file, 1024M") << QStringLiteral("etc/systemd/journald.conf") << QByteArray("1024M");
    }
    void doneWithACapOfAtMostOneGigabyte()
    {
        QFETCH(QString, path);
        QFETCH(QByteArray, size);
        cap(path, size);
        QCOMPARE(state(), gw::CheckState::Done);
    }

    // A larger cap is not done, and the setting systemd would use decides:
    // the later drop-in by name, and /etc's over a vendor file of that name.
    void theCapSystemdUsesDecides()
    {
        cap(QStringLiteral("etc/systemd/journald.conf.d/50-mine.conf"), "4G");
        QCOMPARE(state(), gw::CheckState::NotDone);
        cap(QStringLiteral("usr/lib/systemd/journald.conf.d/60-later.conf"), "512M");
        QCOMPARE(state(), gw::CheckState::Done);
        cap(QStringLiteral("etc/systemd/journald.conf.d/60-later.conf"), "8G");
        QCOMPARE(state(), gw::CheckState::NotDone);
    }

    void applyingWritesTheCapAndRestartsTheLog()
    {
        const QList<gw::Step> steps = gw::LogCapItem().applySteps({}, context());
        QCOMPARE(steps.size(), 3);
        QCOMPARE(steps[0].argv, (QStringList{QStringLiteral("mkdir"), QStringLiteral("-p"),
                                             QStringLiteral("/etc/systemd/journald.conf.d")}));
        QCOMPARE(steps[1].argv.value(1),
                 QStringLiteral("of=/etc/systemd/journald.conf.d/99-groundwork-log-cap.conf"));
        QVERIFY(steps[1].input.contains("\n[Journal]\nSystemMaxUse=1G\nSystemKeepFree=2G\n"
                                        "SystemMaxFileSize=128M\nMaxRetentionSec=3month\n"));
        QCOMPARE(steps[2].argv, (QStringList{QStringLiteral("systemctl"), QStringLiteral("restart"),
                                             QStringLiteral("systemd-journald")}));
        for (const gw::Step &s : steps)
            QVERIFY(s.needsRoot);

        // What it writes is what the check reads as done.
        write(QStringLiteral("etc/systemd/journald.conf.d/99-groundwork-log-cap.conf"), steps[1].input);
        QCOMPARE(state(), gw::CheckState::Done);
    }
};

QTEST_MAIN(TstLogCapItem)
#include "tst_logcapitem.moc"
