// RAM protection (GRND-0052), against a fake systemctl that answers per unit
// from files and a fixture root holding the drop-in files and meminfo.
#include "items/memoryitem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
const QByteArray kSystemctl = R"(#!/bin/sh
cmd=$1; unit=$2
if [ ! -f "$FAKE_UNITS/$unit.enabled" ]; then
  [ "$cmd" = is-enabled ] && echo not-found || echo inactive; exit 4
fi
case $cmd in
  is-enabled) v=$(cat "$FAKE_UNITS/$unit.enabled"); echo "$v"; [ "$v" = enabled ] && exit 0; exit 1;;
  is-active)  v=$(cat "$FAKE_UNITS/$unit.active"); echo "$v"; [ "$v" = active ] && exit 0; exit 3;;
esac
)";

void write(const QString &path, const QByteArray &text)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).path()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text);
}
} // namespace

class TstMemoryItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin;
    std::unique_ptr<QTemporaryDir> m_root, m_units;
    QByteArray m_oldPath;

    void oomd(const QByteArray &enabled, const QByteArray &active)
    {
        write(m_units->filePath(QStringLiteral("systemd-oomd.enabled")), enabled);
        write(m_units->filePath(QStringLiteral("systemd-oomd.active")), active);
    }
    // Protection as the owner set it up by hand, under other file names.
    void handMadeProtection()
    {
        write(m_root->filePath(QStringLiteral("etc/systemd/system/user@.service.d/50-mine.conf")),
              "[Service]\nDelegate=pids memory cpu io\n");
        write(m_root->filePath(QStringLiteral("etc/systemd/user/app.slice.d/50-mine.conf")),
              "[Slice]\nManagedOOMMemoryPressure=kill\n");
    }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }
    static gw::SystemIdentity tumbleweed() { return gw::parseOsRelease("ID=opensuse-tumbleweed\n"); }
    QList<gw::Step> steps() const { return gw::MemoryItem().applySteps(tumbleweed(), context()); }
    static QByteArray inputFor(const QList<gw::Step> &steps, const QString &path)
    {
        for (const gw::Step &s : steps)
            if (s.argv.value(1) == QStringLiteral("of=") + path)
                return s.input;
        return {};
    }

private slots:
    void initTestCase()
    {
        QFile f(m_bin.filePath(QStringLiteral("systemctl")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kSystemctl);
        f.close();
        QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
        m_oldPath = qgetenv("PATH");
        qputenv("PATH", m_bin.path().toUtf8() + ":/usr/bin:/bin");
    }
    void cleanupTestCase() { qputenv("PATH", m_oldPath); }
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        m_units = std::make_unique<QTemporaryDir>();
        qputenv("FAKE_UNITS", m_units->path().toUtf8());
    }

    void watchdogAndSettingsInPlaceIsDone()
    {
        oomd("enabled", "active");
        handMadeProtection();
        QCOMPARE(gw::MemoryItem().check(context()).state, gw::CheckState::Done);
    }

    void watchdogWithoutTheSettingsIsNotDone()
    {
        oomd("enabled", "active");
        QCOMPARE(gw::MemoryItem().check(context()).state, gw::CheckState::NotDone);
    }

    void settingsWithoutTheWatchdogRunningIsNotDone()
    {
        oomd("disabled", "inactive");
        handMadeProtection();
        QCOMPARE(gw::MemoryItem().check(context()).state, gw::CheckState::NotDone);
    }

    void aMissingWatchdogIsInstalledFirst()
    {
        const gw::CheckResult r = gw::MemoryItem().check(context());
        QCOMPARE(r.state, gw::CheckState::NotDone);
        QVERIFY(r.detail.contains(QStringLiteral("systemd-oomd")));
        const auto s = steps();
        QCOMPARE(s.first().argv, QStringList({"zypper", "-n", "install", "systemd-experimental"}));
        QCOMPARE(s.last().argv, QStringList({"systemctl", "enable", "--now", "systemd-oomd"}));
    }

    void anInstalledWatchdogIsNotInstalledAgain()
    {
        oomd("disabled", "inactive");
        QCOMPARE(steps().first().argv.first(), QStringLiteral("mkdir"));
    }

    void noServiceManagerCouldNotTell()
    {
        qputenv("PATH", QByteArray("/nonexistent"));
        const gw::CheckState state = gw::MemoryItem().check(context()).state;
        qputenv("PATH", m_bin.path().toUtf8() + ":/usr/bin:/bin");
        QCOMPARE(state, gw::CheckState::CouldNotTell);
    }

    // Every file is written as root, by dd, with no shell; only app.slice
    // may be killed, and the desktop's session.slice gets the floor.
    void theStepsWriteTheSettings()
    {
        oomd("disabled", "inactive");
        write(m_root->filePath(QStringLiteral("proc/meminfo")), "MemTotal:       32768000 kB\n");
        const auto s = steps();
        for (const gw::Step &step : s)
            QVERIFY(step.needsRoot);
        const QByteArray apps = inputFor(s, QStringLiteral("/etc/systemd/user/app.slice.d/50-groundwork-memory.conf"));
        QVERIFY(apps.contains("ManagedOOMMemoryPressure=kill\n"));
        const QByteArray session =
            inputFor(s, QStringLiteral("/etc/systemd/user/session.slice.d/50-groundwork-memory.conf"));
        QVERIFY(session.contains("MemoryMin=2G\n"));
        QVERIFY(!session.contains("ManagedOOM"));
        QVERIFY(inputFor(s, QStringLiteral("/etc/systemd/system/user@.service.d/50-groundwork-memory.conf"))
                    .contains("Delegate=pids memory cpu io\n"));
        QVERIFY(!inputFor(s, QStringLiteral("/etc/systemd/oomd.conf.d/50-groundwork-memory.conf")).isEmpty());
    }

    // A small machine's desktop gets a smaller floor, so apps keep room.
    void theFloorFollowsTheMachinesMemory()
    {
        QCOMPARE(gw::MemoryItem::floorFor(32LL * 1024 * 1024), QStringLiteral("2G"));
        QCOMPARE(gw::MemoryItem::floorFor(8LL * 1024 * 1024), QStringLiteral("2G"));
        QCOMPARE(gw::MemoryItem::floorFor(6LL * 1024 * 1024), QStringLiteral("1G"));
        QCOMPARE(gw::MemoryItem::floorFor(3LL * 1024 * 1024), QStringLiteral("512M"));
        QCOMPARE(gw::MemoryItem::floorFor(0), QStringLiteral("512M")); // unreadable: the safe end
    }
};

QTEST_MAIN(TstMemoryItem)
#include "tst_memoryitem.moc"
