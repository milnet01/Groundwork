// The smoother scheduler for spinning hard drives (GRND-0054), against a
// fixture root holding each drive's /sys/block entry.
#include "items/diskscheduleritem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstDiskSchedulerItem : public QObject
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
    void drive(const QString &name, const QByteArray &rotational, const QByteArray &scheduler)
    {
        write(QStringLiteral("sys/block/%1/queue/rotational").arg(name), rotational);
        write(QStringLiteral("sys/block/%1/queue/scheduler").arg(name), scheduler);
    }
    gw::CheckState state() const
    {
        return gw::DiskSchedulerItem().check(gw::CheckContext(gw::FileReader(m_root->path()))).state;
    }

private slots:
    void init() { m_root = std::make_unique<QTemporaryDir>(); }

    // Solid-state drives, optical drives and virtual devices never count.
    void notNeededWithoutASpinningDrive()
    {
        drive(QStringLiteral("sda"), "0\n", "none [mq-deadline] kyber bfq\n");
        drive(QStringLiteral("nvme0n1"), "0\n", "[none] mq-deadline kyber bfq\n");
        drive(QStringLiteral("sr0"), "1\n", "none [mq-deadline] kyber bfq\n");
        drive(QStringLiteral("loop0"), "1\n", "[none] mq-deadline\n");
        QCOMPARE(state(), gw::CheckState::NotNeeded);
    }

    void doneOnlyWhenEverySpinningDriveUsesBfq()
    {
        drive(QStringLiteral("sda"), "0\n", "none [mq-deadline] kyber bfq\n");
        drive(QStringLiteral("sdb"), "1\n", "none mq-deadline kyber [bfq]\n");
        QCOMPARE(state(), gw::CheckState::Done);
        drive(QStringLiteral("sdc"), "1\n", "none [mq-deadline] kyber bfq\n");
        QCOMPARE(state(), gw::CheckState::NotDone);
    }

    void anUnreadableDriveCannotBeTold()
    {
        drive(QStringLiteral("sdb"), "1\n", "none mq-deadline kyber [bfq]\n");
        write(QStringLiteral("sys/block/sdc/queue/rotational"), "1\n");
        QCOMPARE(state(), gw::CheckState::CouldNotTell);
    }

    // The rule names no drive, so it holds for a drive added later; udev
    // then applies it to the spinning drives already present.
    void applyingWritesTheRuleAndAppliesIt()
    {
        const QList<gw::Step> steps =
            gw::DiskSchedulerItem().applySteps({}, gw::CheckContext(gw::FileReader(m_root->path())));
        QCOMPARE(steps.size(), 4);
        QCOMPARE(steps[0].argv,
                 (QStringList{QStringLiteral("mkdir"), QStringLiteral("-p"), QStringLiteral("/etc/udev/rules.d")}));
        QCOMPARE(steps[1].argv.value(1), QStringLiteral("of=/etc/udev/rules.d/60-groundwork-iosched.rules"));
        QVERIFY(steps[1].input.contains("ATTR{queue/rotational}==\"1\""));
        QVERIFY(steps[1].input.contains("ATTR{queue/scheduler}=\"bfq\""));
        QCOMPARE(steps[2].argv,
                 (QStringList{QStringLiteral("udevadm"), QStringLiteral("control"), QStringLiteral("--reload")}));
        QCOMPARE(steps[3].argv,
                 (QStringList{QStringLiteral("udevadm"), QStringLiteral("trigger"), QStringLiteral("--action=change"),
                              QStringLiteral("--subsystem-match=block"),
                              QStringLiteral("--attr-match=queue/rotational=1")}));
        for (const gw::Step &s : steps)
            QVERIFY(s.needsRoot);
    }
};

QTEST_MAIN(TstDiskSchedulerItem)
#include "tst_diskscheduleritem.moc"
