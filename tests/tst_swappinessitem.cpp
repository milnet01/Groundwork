// Less eager swapping (GRND-0059), against a fixture root holding the
// kernel's running value.
#include "items/swappinessitem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstSwappinessItem : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_root;

    void swappiness(const QByteArray &value)
    {
        const QString full = m_root->filePath(QStringLiteral("proc/sys/vm/swappiness"));
        QVERIFY(QDir().mkpath(QFileInfo(full).path()));
        QFile f(full);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(value);
    }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }
    gw::CheckState state() const { return gw::SwappinessItem().check(context()).state; }

private slots:
    void init() { m_root = std::make_unique<QTemporaryDir>(); }

    void doneAtTenOrLower_data()
    {
        QTest::addColumn<QByteArray>("value");
        QTest::addColumn<gw::CheckState>("expected");
        QTest::newRow("kernel default") << QByteArray("60\n") << gw::CheckState::NotDone;
        QTest::newRow("11") << QByteArray("11\n") << gw::CheckState::NotDone;
        QTest::newRow("10") << QByteArray("10\n") << gw::CheckState::Done;
        QTest::newRow("lower, left alone") << QByteArray("1\n") << gw::CheckState::Done;
        QTest::newRow("unreadable") << QByteArray("") << gw::CheckState::CouldNotTell;
    }
    void doneAtTenOrLower()
    {
        QFETCH(QByteArray, value);
        QFETCH(gw::CheckState, expected);
        swappiness(value);
        QCOMPARE(state(), expected);
    }

    void applyingSetsItNowAndAtEveryStart()
    {
        const QList<gw::Step> steps = gw::SwappinessItem().applySteps({}, context());
        QCOMPARE(steps.size(), 3);
        QCOMPARE(steps[0].argv, (QStringList{QStringLiteral("mkdir"), QStringLiteral("-p"),
                                             QStringLiteral("/etc/sysctl.d")}));
        QCOMPARE(steps[1].argv.value(1), QStringLiteral("of=/etc/sysctl.d/99-groundwork-swappiness.conf"));
        QVERIFY(steps[1].input.endsWith("\nvm.swappiness = 10\n"));
        QCOMPARE(steps[2].argv, (QStringList{QStringLiteral("sysctl"), QStringLiteral("-p"),
                                             QStringLiteral("/etc/sysctl.d/99-groundwork-swappiness.conf")}));
        for (const gw::Step &s : steps)
            QVERIFY(s.needsRoot);
    }
};

QTEST_MAIN(TstSwappinessItem)
#include "tst_swappinessitem.moc"
