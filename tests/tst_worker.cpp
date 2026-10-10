// The Worker end to end: the built program, a fixture root, and fake sudo,
// zypper and flatpak alone on PATH, each recording its calls. Nothing real
// is read, run, or run as root.
#include "core/markers.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

namespace {

const QByteArray kSudo = R"(#!/bin/sh
echo "sudo $*" >> "$FAKE_LOG"
case " $* " in *" -v "*)
  if [ "$1" != "-n" ] && [ -n "$FAKE_SUDO_FAIL" ]; then exit 1; fi
  exit 0;;
esac
while [ $# -gt 0 ]; do case "$1" in -n|-A) shift;; --) shift; break;; *) break;; esac; done
exec "$@"
)";

const QByteArray kZypper = R"(#!/bin/sh
echo "zypper $*" >> "$FAKE_LOG"
case " $* " in *" list-updates "*) printf '<stream><update-list></update-list></stream>'; exit 0;; esac
case " $* " in *" needs-rebooting "*) exit "${FAKE_REBOOT:-0}";; esac
if [ -n "$FAKE_STOP_FILE" ]; then touch "$FAKE_STOP_FILE"; fi
if [ -s "$FAKE_ZYPPER_CODES" ]; then
  code=$(head -n1 "$FAKE_ZYPPER_CODES"); sed -i 1d "$FAKE_ZYPPER_CODES"; exit "$code"
fi
exit 0
)";

const QByteArray kFlatpak = R"(#!/bin/sh
echo "flatpak $*" >> "$FAKE_LOG"
case "$1" in
  remotes) if [ -f "$FAKE_FLATHUB" ]; then printf 'flathub\tsystem\n'; fi; exit 0;;
  remote-add) touch "$FAKE_FLATHUB"; exit 0;;
esac
exit 0
)";

} // namespace

class TstWorker : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_bin, m_root, m_state, m_log, m_flathub, m_codes;
    QProcessEnvironment m_env;

    void writeFile(const QString &path, const QByteArray &text, bool executable = false)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).path()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
        f.close();
        if (executable)
            QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
    }

    struct Run
    {
        int exitCode = -1;
        QList<gw::Marker> markers;
        QStringList calls;
        QStringList endsFor(const QString &id) const
        {
            for (const auto &m : markers)
                if (m.name == QLatin1String("STEP_END") && m.fields.value(0) == id)
                    return m.fields;
            return {};
        }
        int count(const QString &prefix) const
        {
            int n = 0;
            for (const auto &c : calls)
                n += c.startsWith(prefix) ? 1 : 0;
            return n;
        }
    };

    Run run(const QStringList &ids)
    {
        QProcess p;
        p.setProcessEnvironment(m_env);
        p.start(QStringLiteral(GROUNDWORK_BIN), QStringList{QStringLiteral("--worker")} + ids);
        p.waitForFinished(30000);
        Run r;
        r.exitCode = p.exitCode();
        for (const QString &line : QString::fromUtf8(p.readAllStandardOutput()).split(QLatin1Char('\n')))
            if (auto m = gw::parseMarker(line))
                r.markers << *m;
        QFile log(m_log);
        if (log.open(QIODevice::ReadOnly))
            r.calls = QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        return r;
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_bin = m_dir->filePath(QStringLiteral("bin"));
        m_root = m_dir->filePath(QStringLiteral("root"));
        m_state = m_dir->filePath(QStringLiteral("state"));
        m_log = m_dir->filePath(QStringLiteral("calls.log"));
        m_flathub = m_dir->filePath(QStringLiteral("flathub-added"));
        m_codes = m_dir->filePath(QStringLiteral("zypper-codes"));
        writeFile(m_bin + QStringLiteral("/sudo"), kSudo, true);
        writeFile(m_bin + QStringLiteral("/zypper"), kZypper, true);
        writeFile(m_bin + QStringLiteral("/flatpak"), kFlatpak, true);
        writeFile(m_root + QStringLiteral("/etc/os-release"), "ID=opensuse-tumbleweed\n");

        m_env = QProcessEnvironment();
        // sh, sed, head and touch for the fakes themselves; the fakes come first.
        m_env.insert(QStringLiteral("PATH"), m_bin + QStringLiteral(":/usr/bin:/bin"));
        m_env.insert(QStringLiteral("HOME"), m_dir->path());
        m_env.insert(QStringLiteral("GROUNDWORK_ROOT"), m_root);
        m_env.insert(QStringLiteral("XDG_STATE_HOME"), m_state);
        m_env.insert(QStringLiteral("FAKE_LOG"), m_log);
        m_env.insert(QStringLiteral("FAKE_FLATHUB"), m_flathub);
        m_env.insert(QStringLiteral("FAKE_ZYPPER_CODES"), m_codes);
    }

    void alreadyDoneRunsNothingAndAsksNoPassword()
    {
        writeFile(m_flathub, "");
        const Run r = run({QStringLiteral("flathub")});
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(r.endsFor(QStringLiteral("flathub")).value(1), QStringLiteral("skip"));
        QCOMPARE(r.count(QStringLiteral("sudo")), 0);
    }

    void freshSystemRunsEveryStepAsRootAfterOnePassword()
    {
        const Run r = run({QStringLiteral("system-update"), QStringLiteral("flathub")});
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(r.endsFor(QStringLiteral("system-update")).value(1), QStringLiteral("ok"));
        QCOMPARE(r.endsFor(QStringLiteral("flathub")).value(1), QStringLiteral("ok"));
        QCOMPARE(r.calls.count(QStringLiteral("sudo -v")), 1);          // one password
        QVERIFY(r.calls.contains(QStringLiteral("sudo -n -- zypper -n dup")));
        QVERIFY(r.calls.contains(QStringLiteral("sudo -n -- flatpak remote-add --if-not-exists flathub "
                                                "https://dl.flathub.org/repo/flathub.flatpakrepo")));
        QVERIFY(QFile::exists(m_flathub));
    }

    void zypper103RunsTheStepOnceMore()
    {
        writeFile(m_codes, "0\n103\n0\n"); // refresh, dup (103), dup again
        const Run r = run({QStringLiteral("system-update")});
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(r.count(QStringLiteral("zypper -n dup")), 2);
    }

    void zypper106SucceedsAndTellsTheUser()
    {
        writeFile(m_codes, "106\n0\n");
        const Run r = run({QStringLiteral("system-update")});
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(r.endsFor(QStringLiteral("system-update")).value(1), QStringLiteral("ok"));
        bool hint = false;
        for (const auto &m : r.markers)
            hint = hint || m.name == QLatin1String("HINT");
        QVERIFY(hint);
    }

    static bool hasRestartHint(const Run &r)
    {
        for (const auto &m : r.markers)
            if (m.name == QLatin1String("HINT") && m.fields.value(1).contains(QLatin1String("restart"), Qt::CaseInsensitive))
                return true;
        return false;
    }

    // zypper says a restart is suggested after core libraries changed; the
    // summary alone said "All done." (GRND-0048).
    void aSuggestedRestartIsSaid()
    {
        m_env.insert(QStringLiteral("FAKE_REBOOT"), QStringLiteral("102"));
        const Run r = run({QStringLiteral("system-update")});
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(r.count(QStringLiteral("zypper needs-rebooting")), 1);
        QVERIFY(hasRestartHint(r));
    }

    void noRestartIsSaidWhenNoneIsSuggested()
    {
        const Run r = run({QStringLiteral("system-update")});
        QCOMPARE(r.count(QStringLiteral("zypper needs-rebooting")), 1);
        QVERIFY(!hasRestartHint(r));
    }

    void aRunThatChangedNothingDoesNotAsk()
    {
        writeFile(m_flathub, "");
        m_env.insert(QStringLiteral("FAKE_REBOOT"), QStringLiteral("102"));
        const Run r = run({QStringLiteral("flathub")});
        QCOMPARE(r.count(QStringLiteral("zypper needs-rebooting")), 0);
        QVERIFY(!hasRestartHint(r));
    }

    void aFailureSkipsWhatDependsOnIt()
    {
        writeFile(m_codes, "0\n4\n"); // refresh ok, dup fails
        const Run r = run({QStringLiteral("system-update"), QStringLiteral("flathub")});
        QCOMPARE(r.exitCode, 1);
        QCOMPARE(r.endsFor(QStringLiteral("system-update")).value(1), QStringLiteral("fail"));
        QCOMPARE(r.endsFor(QStringLiteral("flathub")).value(1), QStringLiteral("skip"));
        QCOMPARE(r.count(QStringLiteral("flatpak remote-add")), 0);
    }

    void stopsBetweenItems()
    {
        m_env.insert(QStringLiteral("FAKE_STOP_FILE"), m_state + QStringLiteral("/groundwork/stop.request"));
        const Run r = run({QStringLiteral("system-update"), QStringLiteral("flathub")});
        QCOMPARE(r.exitCode, 5);
        QVERIFY(r.endsFor(QStringLiteral("flathub")).isEmpty()); // never started
        QCOMPARE(r.markers.last().name, QStringLiteral("DONE"));
        QCOMPARE(r.markers.last().fields.value(2), QStringLiteral("1"));
    }

    void anOldStopFileDoesNotStopANewRun()
    {
        writeFile(m_state + QStringLiteral("/groundwork/stop.request"), "");
        const Run r = run({QStringLiteral("system-update")});
        QCOMPARE(r.exitCode, 0);
    }

    void refusedPasswordStopsTheRun()
    {
        m_env.insert(QStringLiteral("FAKE_SUDO_FAIL"), QStringLiteral("1"));
        const Run r = run({QStringLiteral("system-update")});
        QCOMPARE(r.exitCode, 4);
        QCOMPARE(r.count(QStringLiteral("sudo -n --")), 0);
    }

    void refusesAnUnsupportedSystem()
    {
        writeFile(m_root + QStringLiteral("/etc/os-release"), "ID=opensuse-leap\nVERSION_ID=15.6\n");
        const Run r = run({QStringLiteral("system-update")});
        QCOMPARE(r.exitCode, 3);
        QCOMPARE(r.count(QStringLiteral("sudo")), 0);
    }

    void aStepsExtraSuccessCodeCountsAsOk()
    {
        // Firmware updates waiting; `fwupdmgr update` then finds nothing
        // to do and exits 2, which that step accepts.
        writeFile(m_bin + QStringLiteral("/fwupdmgr"), R"(#!/bin/sh
echo "fwupdmgr $*" >> "$FAKE_LOG"
case "$1" in
  get-updates) printf '{"Devices":[{"Releases":[{"Version":"2"}]}]}';;
  update) exit 2;;
esac
)", true);
        const Run r = run({QStringLiteral("firmware-updates")});
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(r.endsFor(QStringLiteral("firmware-updates")).value(1), QStringLiteral("ok"));
        QCOMPARE(r.count(QStringLiteral("fwupdmgr update")), 1);
    }

    void aValueReachesItsCommandAndAMissingOneSkips()
    {
        writeFile(m_bin + QStringLiteral("/hostnamectl"), R"(#!/bin/sh
echo "hostnamectl $*" >> "$FAKE_LOG"
[ "$2" = --static ] && echo localhost
exit 0
)", true);
        Run r = run({QStringLiteral("--set"), QStringLiteral("computer-name=lounge-pc"), QStringLiteral("computer-name")});
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(r.endsFor(QStringLiteral("computer-name")).value(1), QStringLiteral("ok"));
        QVERIFY(r.calls.contains(QStringLiteral("sudo -n -- hostnamectl hostname lounge-pc")));

        QFile::remove(m_log);
        r = run({QStringLiteral("computer-name")}); // no --set
        QCOMPARE(r.endsFor(QStringLiteral("computer-name")).value(1), QStringLiteral("skip"));
        QCOMPARE(r.count(QStringLiteral("hostnamectl hostname lounge")), 0);

        r = run({QStringLiteral("--set"), QStringLiteral("computer-name=-bad-"), QStringLiteral("computer-name")});
        QCOMPARE(r.endsFor(QStringLiteral("computer-name")).value(1), QStringLiteral("skip"));
    }

    void refusesAnUnknownItem()
    {
        QCOMPARE(run({QStringLiteral("no-such-item")}).exitCode, 2);
    }
};

QTEST_GUILESS_MAIN(TstWorker)
#include "tst_worker.moc"
