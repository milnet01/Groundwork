// The freeze recorder (GRND-0058), against a fake systemctl that answers
// per unit from files; and its script, run once against this machine.
#include "items/freezerecorderitem.h"

#include <QDir>
#include <QFile>
#include <QProcess>
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
const QString kScript = QStringLiteral("/usr/local/bin/freeze-recorder");
const QString kUnit = QStringLiteral("/etc/systemd/system/freeze-recorder.service");

void write(const QString &path, const QByteArray &text)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).path()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text);
}

QByteArray inputFor(const QList<gw::Step> &steps, const QString &path)
{
    for (const gw::Step &s : steps)
        if (s.argv.value(1) == QStringLiteral("of=") + path)
            return s.input;
    return {};
}
} // namespace

class TstFreezeRecorderItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin;
    std::unique_ptr<QTemporaryDir> m_units;
    QByteArray m_oldPath;

    void recorder(const QByteArray &enabled, const QByteArray &active)
    {
        write(m_units->filePath(QStringLiteral("freeze-recorder.service.enabled")), enabled);
        write(m_units->filePath(QStringLiteral("freeze-recorder.service.active")), active);
    }
    gw::CheckState state() const
    {
        return gw::FreezeRecorderItem().check(gw::CheckContext(gw::FileReader(m_units->path()))).state;
    }
    static QList<gw::Step> steps() { return gw::FreezeRecorderItem().applySteps({}, gw::CheckContext(gw::FileReader())); }

    // A /proc holding pressure and memory readings, so a run does not depend
    // on how busy this machine is: memory pressure as given, nothing else.
    void proc(const QByteArray &memorySome)
    {
        const QByteArray calm = "some avg10=0.00 avg60=0.00 avg300=0.00 total=0\n"
                                "full avg10=0.00 avg60=0.00 avg300=0.00 total=0\n";
        write(m_units->filePath(QStringLiteral("proc/pressure/memory")),
              "some avg10=" + memorySome + " avg60=0.00 avg300=0.00 total=0\n"
              "full avg10=0.00 avg60=0.00 avg300=0.00 total=0\n");
        write(m_units->filePath(QStringLiteral("proc/pressure/io")), calm);
        write(m_units->filePath(QStringLiteral("proc/pressure/cpu")), calm);
        write(m_units->filePath(QStringLiteral("proc/meminfo")),
              "MemTotal: 16000000 kB\nMemAvailable: 8000000 kB\nSwapTotal: 0 kB\nSwapFree: 0 kB\n");
    }

    // Runs the script the item installs once against proc(), with the given
    // settings, and returns the ring log's lines.
    QStringList runOnce(QStringList env, const QByteArray &ringBefore = {})
    {
        env << QStringLiteral("PROC=") + m_units->filePath(QStringLiteral("proc"));
        const QString script = m_units->filePath(QStringLiteral("freeze-recorder"));
        const QString ring = m_units->filePath(QStringLiteral("ring.log"));
        write(script, inputFor(steps(), kScript));
        if (!ringBefore.isEmpty())
            write(ring, ringBefore);
        QProcess p;
        p.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
        QProcessEnvironment e = p.processEnvironment();
        e.insert(QStringLiteral("ONESHOT"), QStringLiteral("1"));
        e.insert(QStringLiteral("RING"), ring);
        for (const QString &kv : env)
            e.insert(kv.section(QLatin1Char('='), 0, 0), kv.section(QLatin1Char('='), 1));
        p.setProcessEnvironment(e);
        p.start(QStringLiteral("/bin/bash"), {script});
        if (!p.waitForFinished(10000) || p.exitCode() != 0)
            return {QStringLiteral("script failed: ") + QString::fromUtf8(p.readAllStandardError())};
        QFile f(ring);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

private slots:
    void initTestCase()
    {
        write(m_bin.filePath(QStringLiteral("systemctl")), kSystemctl);
        QFile f(m_bin.filePath(QStringLiteral("systemctl")));
        QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
        m_oldPath = qgetenv("PATH");
        qputenv("PATH", m_bin.path().toUtf8() + ":/usr/bin:/bin");
    }
    void cleanupTestCase() { qputenv("PATH", m_oldPath); }
    void init()
    {
        m_units = std::make_unique<QTemporaryDir>();
        qputenv("FAKE_UNITS", m_units->path().toUtf8());
    }

    void doneOnlyWhileTheRecorderRunsAndStartsAtBoot()
    {
        QCOMPARE(state(), gw::CheckState::NotDone);
        recorder("enabled", "inactive");
        QCOMPARE(state(), gw::CheckState::NotDone);
        recorder("disabled", "active");
        QCOMPARE(state(), gw::CheckState::NotDone);
        recorder("enabled", "active");
        QCOMPARE(state(), gw::CheckState::Done);
    }

    // The script and the service are written, then started; a recorder
    // already running is restarted, so it runs the new script.
    void applyingInstallsAndStartsTheRecorder()
    {
        const QList<gw::Step> s = steps();
        QCOMPARE(s.size(), 7);
        QCOMPARE(s[0].argv, (QStringList{QStringLiteral("mkdir"), QStringLiteral("-p"),
                                         QStringLiteral("/usr/local/bin")}));
        QCOMPARE(s[1].argv.value(1), QStringLiteral("of=") + kScript);
        QCOMPARE(s[2].argv, (QStringList{QStringLiteral("chmod"), QStringLiteral("755"), kScript}));
        QCOMPARE(s[3].argv.value(1), QStringLiteral("of=") + kUnit);
        QCOMPARE(s[4].argv, (QStringList{QStringLiteral("systemctl"), QStringLiteral("daemon-reload")}));
        QCOMPARE(s[5].argv, (QStringList{QStringLiteral("systemctl"), QStringLiteral("enable"),
                                         QStringLiteral("freeze-recorder.service")}));
        QCOMPARE(s[6].argv, (QStringList{QStringLiteral("systemctl"), QStringLiteral("restart"),
                                         QStringLiteral("freeze-recorder.service")}));
        for (const gw::Step &step : s)
            QVERIFY(step.needsRoot);
    }

    // It must keep running, and keep writing, while the machine starves.
    void theServiceIsProtected()
    {
        const QByteArray unit = inputFor(steps(), kUnit);
        for (const char *line : {"ExecStart=/usr/local/bin/freeze-recorder", "Restart=always", "OOMScoreAdjust=-900",
                                 "Nice=-10", "IOSchedulingClass=realtime", "MemoryMin=32M", "CPUWeight=200",
                                 "ProtectSystem=strict", "ReadWritePaths=/var/log", "WantedBy=multi-user.target"})
            QVERIFY2(unit.contains(QByteArray(line) + '\n'), line);
    }

    // At 10% memory pressure it names what was using the memory.
    void underPressureItNamesWhatUsesMemory()
    {
        proc("10.00");
        const QStringList lines = runOnce({});
        QCOMPARE(lines.size(), 3);
        QVERIFY2(lines[0].contains(QStringLiteral("PRESSURE RISING — mem_psi=10/0 ")), qPrintable(lines[0]));
        QVERIFY(lines[1].contains(QLatin1String(" SAMPLE mem_psi=")));
        QVERIFY(lines[2].contains(QLatin1String(" TOP-RSS ")));
    }

    // Below it, the first sample is a single calm line.
    void belowThePressureItIsCalm()
    {
        proc("9.99");
        const QStringList lines = runOnce({});
        QCOMPARE(lines.size(), 1);
        QVERIFY2(lines[0].contains(QLatin1String(" calm mem_psi=9.99/0 ")), qPrintable(lines[0]));
    }

    // Past RING_MAX lines the oldest go first, down to RING_KEEP.
    void theRingLogDropsItsOldestLines()
    {
        QByteArray before;
        for (int i = 0; i < 10; ++i)
            before += "old line " + QByteArray::number(i) + '\n';
        proc("50.00");
        const QStringList lines = runOnce({QStringLiteral("RING_MAX=5"),
                                           QStringLiteral("RING_KEEP=2"), QStringLiteral("RING_TRIM_EVERY=1")},
                                          before);
        QCOMPARE(lines.size(), 3);
        QVERIFY(lines[0].contains(QLatin1String(" SAMPLE ")));
        QVERIFY(lines[1].contains(QLatin1String(" TOP-RSS ")));
        QVERIFY2(lines[2].endsWith(QLatin1String("ring trimmed: dropped 11 oldest lines")), qPrintable(lines[2]));
    }
};

QTEST_MAIN(TstFreezeRecorderItem)
#include "tst_freezerecorderitem.moc"
