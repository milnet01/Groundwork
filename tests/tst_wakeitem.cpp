// The calmer wake from hibernation (GRND-0055), against a fixture root
// holding the maintenance timers' unit files and drop-ins.
#include "items/wakeitem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstWakeItem : public QObject
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
    void timer(const QString &name, const QByteArray &timerSection)
    {
        write(QStringLiteral("usr/lib/systemd/system/%1.timer").arg(name),
              "[Unit]\nDescription=x\n\n[Timer]\n" + timerSection + "\n[Install]\nWantedBy=timers.target\n");
    }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }
    gw::CheckState state() const { return gw::WakeItem().check(context()).state; }

private slots:
    void init() { m_root = std::make_unique<QTemporaryDir>(); }

    void notNeededWithoutAnyOfTheTimers()
    {
        timer(QStringLiteral("fstrim"), "OnCalendar=weekly\n");
        QCOMPARE(state(), gw::CheckState::NotNeeded);
    }

    // A delay in the unit file or in a drop-in counts; a later 0 cancels it.
    void doneOnlyWhenEveryTimerFoundHasADelay()
    {
        timer(QStringLiteral("backup-rpmdb"), "OnCalendar=daily\nRandomizedDelaySec=2h\nPersistent=true\n");
        timer(QStringLiteral("snapper-timeline"), "OnCalendar=hourly\n");
        QCOMPARE(state(), gw::CheckState::NotDone);
        write(QStringLiteral("etc/systemd/system/snapper-timeline.timer.d/50-mine.conf"),
              "[Timer]\nRandomizedDelaySec=10min\n");
        QCOMPARE(state(), gw::CheckState::Done);
        write(QStringLiteral("etc/systemd/system/backup-rpmdb.timer.d/90-off.conf"), "[Timer]\nRandomizedDelaySec=0\n");
        QCOMPARE(state(), gw::CheckState::NotDone);
    }

    // Only the timers without a delay get one, so a longer delay the
    // distribution ships is never shortened.
    void applyingSpreadsOnlyTheTimersWithoutADelay()
    {
        timer(QStringLiteral("backup-rpmdb"), "OnCalendar=daily\nRandomizedDelaySec=2h\nPersistent=true\n");
        timer(QStringLiteral("snapper-timeline"), "OnCalendar=hourly\n");
        timer(QStringLiteral("unbound-anchor"), "OnCalendar=daily\nPersistent=true\n");
        const QList<gw::Step> steps = gw::WakeItem().applySteps({}, context());
        QCOMPARE(steps.size(), 4);
        QCOMPARE(steps[0].argv,
                 (QStringList{QStringLiteral("mkdir"), QStringLiteral("-p"),
                              QStringLiteral("/etc/systemd/system/snapper-timeline.timer.d"),
                              QStringLiteral("/etc/systemd/system/unbound-anchor.timer.d")}));
        QCOMPARE(steps[1].argv.value(1),
                 QStringLiteral("of=/etc/systemd/system/snapper-timeline.timer.d/99-groundwork-spread.conf"));
        QCOMPARE(steps[2].argv.value(1),
                 QStringLiteral("of=/etc/systemd/system/unbound-anchor.timer.d/99-groundwork-spread.conf"));
        QVERIFY(steps[1].input.contains("\n[Timer]\nRandomizedDelaySec=30min\n"));
        QCOMPARE(steps[3].argv, (QStringList{QStringLiteral("systemctl"), QStringLiteral("daemon-reload")}));
        for (const gw::Step &s : steps)
            QVERIFY(s.needsRoot);
    }
};

QTEST_MAIN(TstWakeItem)
#include "tst_wakeitem.moc"
