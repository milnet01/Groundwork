// The Worker end to end: the built program, a fixture root, and fake sudo,
// zypper and flatpak alone on PATH, each recording its calls. Nothing real
// is read, run, or run as root.
#include "core/markers.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <unistd.h>

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
case " $* " in *" list-updates "*) printf '<stream><update-list></update-list></stream>'; exit "${FAKE_LIST_CODE:-0}";; esac
case " $* " in *" needs-rebooting "*) exit "${FAKE_REBOOT:-0}";; esac
if [ -n "$FAKE_ECHO" ]; then echo "$FAKE_ECHO"; fi
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

// Preparing a drive (GRND-0076): lsblk shows the drive as it was until the
// fake sfdisk has written a table, then as it is after.
const QByteArray kLsblk = R"(#!/bin/sh
echo "lsblk $*" >> "$FAKE_LOG"
if [ -f "$FAKE_TABLE" ]; then cat "$FAKE_LSBLK_AFTER"; else cat "$FAKE_LSBLK"; fi
)";

const QByteArray kSfdisk = R"(#!/bin/sh
echo "sfdisk $*" >> "$FAKE_LOG"
cat > "$FAKE_TABLE"
)";

// udevadm and every mkfs: record the call, succeed.
const QByteArray kRecord = R"(#!/bin/sh
echo "${0##*/} $*" >> "$FAKE_LOG"
)";

// Saving a setup (GRND-0078): the real tar, unless told to fail writing.
const QByteArray kTar = R"(#!/bin/sh
echo "tar $*" >> "$FAKE_LOG"
if [ "$1" = --create ] && [ -n "$FAKE_TAR_FAIL" ]; then cat > /dev/null; exit 2; fi
exec /usr/bin/tar "$@"
)";

// Restoring a setup (GRND-0079): rpm finds what the fake zypper installed;
// zypper installs every name but those starting "nosuch".
const QByteArray kRpm = R"(#!/bin/sh
echo "rpm $*" >> "$FAKE_LOG"
shift
missing=0
for name in "$@"; do
  if grep -qx "$name" "$FAKE_INSTALLED" 2>/dev/null; then echo "$name-1.0"; else echo "package $name is not installed"; missing=$((missing+1)); fi
done
exit $missing
)";

const QByteArray kZypperInstalls = R"(#!/bin/sh
echo "zypper $*" >> "$FAKE_LOG"
case " $* " in *" list-updates "*) printf '<stream><update-list></update-list></stream>'; exit 0;; esac
case " $* " in *" install "*)
  for name in "$@"; do case "$name" in -*|install|nosuch*) ;; *) echo "$name" >> "$FAKE_INSTALLED";; esac; done;;
esac
exit 0
)";

// zypper -n installs nothing and exits 4 while any name clashes
// (GRND-0079's VM run, 2026-10-26); otherwise as kZypperInstalls.
const QByteArray kZypperClashes = R"(#!/bin/sh
echo "zypper $*" >> "$FAKE_LOG"
case " $* " in *" list-updates "*) printf '<stream><update-list></update-list></stream>'; exit 0;; esac
case " $* " in *" clash-"*) exit 4;; esac
for name in "$@"; do case "$name" in -*|install|nosuch*) ;; *) echo "$name" >> "$FAKE_INSTALLED";; esac; done
exit 0
)";

// As kZypperClashes, but an install that works also skips an unreadable
// source, exit 106.
const QByteArray kZypperClashesAndSkips = R"(#!/bin/sh
echo "zypper $*" >> "$FAKE_LOG"
case " $* " in *" list-updates "*) printf '<stream><update-list></update-list></stream>'; exit 0;; esac
case " $* " in *" clash-"*) exit 4;; esac
case " $* " in *" install "*)
  for name in "$@"; do case "$name" in -*|install|nosuch*) ;; *) echo "$name" >> "$FAKE_INSTALLED";; esac; done
  exit 106;;
esac
exit 0
)";

// Every install exits 4, as when the system itself fails: zypper(8) says
// 4 is any error from the ZYPP library, not only a clash.
const QByteArray kZypperInstallsNothing = R"(#!/bin/sh
echo "zypper $*" >> "$FAKE_LOG"
case " $* " in *" list-updates "*) printf '<stream><update-list></update-list></stream>'; exit 0;; esac
case " $* " in *" install "*) exit 4;; esac
exit 0
)";

constexpr qint64 kMiB = qint64(1) << 20;
constexpr qint64 kGiB = qint64(1) << 30;

struct FakePart
{
    qint64 size;
    QString fstype, label;
};

// One GPT drive, /dev/sdb, in the shape lsblk --json --bytes prints; its
// partitions laid end to end from 1 MiB.
QByteArray lsblkJson(qint64 size, const QList<FakePart> &parts, const QString &serial = QStringLiteral("S123"))
{
    auto orNull = [](const QString &s) { return s.isEmpty() ? QJsonValue() : QJsonValue(s); };
    QJsonArray children;
    qint64 start = kMiB;
    for (qsizetype i = 0; i < parts.size(); ++i) {
        const FakePart &p = parts[i];
        children.append(QJsonObject{{"name", QStringLiteral("sdb%1").arg(i + 1)},
                                    {"path", QStringLiteral("/dev/sdb%1").arg(i + 1)},
                                    {"type", "part"}, {"size", double(p.size)}, {"start", double(start) / 512},
                                    {"fstype", orNull(p.fstype)}, {"label", orNull(p.label)},
                                    {"mountpoints", QJsonArray{QJsonValue()}}});
        start += p.size;
    }
    QJsonObject disk{{"name", "sdb"}, {"path", "/dev/sdb"}, {"type", "disk"}, {"size", double(size)},
                     {"pttype", "gpt"}, {"ptuuid", "af947acd-e52f-4faf-a236-48385a2a8a58"},
                     {"mountpoints", QJsonArray{QJsonValue()}}, {"rm", false}, {"ro", false}, {"rota", false},
                     {"tran", "usb"}, {"model", "Test Disk"}, {"serial", serial}, {"log-sec", 512}};
    if (!children.isEmpty())
        disk.insert("children", children);
    return QJsonDocument(QJsonObject{{"blockdevices", QJsonArray{disk}}}).toJson();
}

// A blank 30 GiB drive: the whole-drive layout leaves the system 24 GiB
// less the 2 MiB GPT keeps at its two ends.
constexpr qint64 kDriveSize = 30 * kGiB;
constexpr qint64 kSystemSize = 24 * kGiB - 2 * kMiB;

QList<FakePart> preparedParts()
{
    return {{4 * kGiB, "vfat", "EFI"}, {2 * kGiB, "swap", "swap"}, {kSystemSize, "btrfs", "openSUSE"}};
}

QJsonObject wholeDriveLayout()
{
    return {{"format", 1},
            {"drive", QJsonObject{{"path", "/dev/sdb"}, {"size", double(kDriveSize)}, {"model", "Test Disk"},
                                  {"serial", "S123"}, {"ptuuid", "af947acd-e52f-4faf-a236-48385a2a8a58"}}},
            {"firmware", "uefi"},
            {"kind", "whole"},
            {"partitions", QJsonArray{QJsonObject{{"use", "efi"}, {"size", double(4 * kGiB)}},
                                      QJsonObject{{"use", "swap"}, {"size", double(2 * kGiB)}},
                                      QJsonObject{{"use", "system"}, {"size", QJsonValue()}}}}};
}

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
        return finish(p);
    }

    // A UEFI computer whose lsblk shows a blank drive, then `after` once a
    // table is written; the layout file to prepare it with.
    void setUpDrive(const QJsonObject &layout, const QByteArray &after)
    {
        for (const char *tool : {"udevadm", "mkfs.fat", "mkfs.btrfs", "mkfs.xfs", "mkswap"})
            writeFile(m_bin + QLatin1Char('/') + QLatin1String(tool), kRecord, true);
        writeFile(m_bin + QStringLiteral("/lsblk"), kLsblk, true);
        writeFile(m_bin + QStringLiteral("/sfdisk"), kSfdisk, true);
        writeFile(m_dir->filePath(QStringLiteral("lsblk-before")), lsblkJson(kDriveSize, {}));
        writeFile(m_dir->filePath(QStringLiteral("lsblk-after")), after);
        QVERIFY(QDir().mkpath(m_root + QStringLiteral("/sys/firmware/efi")));
        writeFile(m_dir->filePath(QStringLiteral("layout.json")), QJsonDocument(layout).toJson());
        m_env.insert(QStringLiteral("FAKE_LSBLK"), m_dir->filePath(QStringLiteral("lsblk-before")));
        m_env.insert(QStringLiteral("FAKE_LSBLK_AFTER"), m_dir->filePath(QStringLiteral("lsblk-after")));
        m_env.insert(QStringLiteral("FAKE_TABLE"), m_dir->filePath(QStringLiteral("table")));
    }

    Run prepare()
    {
        QProcess p;
        p.setProcessEnvironment(m_env);
        p.start(QStringLiteral(GROUNDWORK_BIN), {QStringLiteral("--worker"), QStringLiteral("--prepare-drive"),
                                                 m_dir->filePath(QStringLiteral("layout.json"))});
        return finish(p);
    }

    // A home with a Dolphin setting and an SSH key; the choices file that
    // saves the settings at paths to output.
    QString saveSetup(const QJsonArray &paths, const QString &output)
    {
        writeFile(m_bin + QStringLiteral("/tar"), kTar, true);
        writeFile(m_dir->filePath(QStringLiteral(".config/dolphinrc")), "[General]\n");
        writeFile(m_dir->filePath(QStringLiteral(".ssh/id_ed25519")), "not a real key\n");
        const QJsonObject manifest{
            {"format", 1},
            {"packages", QJsonArray{"act"}},
            {"settings", QJsonArray{QJsonObject{{"name", "Mine"}, {"paths", paths}, {"secret", true}, {"bytes", 30}}}}};
        const QString choices = m_dir->filePath(QStringLiteral("setup-choices.json"));
        writeFile(choices, QJsonDocument(QJsonObject{{"output", output}, {"manifest", manifest}}).toJson());
        return choices;
    }

    Run save(const QString &choices)
    {
        QProcess p;
        p.setProcessEnvironment(m_env);
        p.start(QStringLiteral(GROUNDWORK_BIN), {QStringLiteral("--worker"), QStringLiteral("--save-setup"), choices});
        return finish(p);
    }

    // A saved file (GRND-0078's format, written with the real tar) holding
    // Dolphin's settings and an SSH key, and keeping two programs.
    QString savedFile(const QJsonObject &manifest = savedManifest())
    {
        writeFile(m_bin + QStringLiteral("/tar"), kTar, true);
        writeFile(m_dir->filePath(QStringLiteral(".config/dolphinrc")), "[General]\nShowFullPath=true\n");
        writeFile(m_dir->filePath(QStringLiteral(".ssh/id_ed25519")), "not a real key\n");
        const QString staged = m_dir->filePath(QStringLiteral("staged"));
        writeFile(staged + QStringLiteral("/groundwork-setup.json"), QJsonDocument(manifest).toJson());
        const QString file = m_dir->filePath(QStringLiteral("groundwork-setup-pc-2026-10-20.tar"));
        QProcess tar;
        tar.start(QStringLiteral("/usr/bin/tar"), {QStringLiteral("--create"), QStringLiteral("--format=posix"),
                                                   QStringLiteral("--file"), file, QStringLiteral("-C"), staged,
                                                   QStringLiteral("groundwork-setup.json"), QStringLiteral("-C"),
                                                   m_dir->path(), QStringLiteral(".config/dolphinrc"), QStringLiteral(".ssh")});
        tar.waitForFinished();
        return tar.exitCode() == 0 ? file : QString();
    }
    static QJsonObject savedManifest()
    {
        return {{"format", 1},
                {"system", QJsonObject{{"id", "opensuse-tumbleweed"}}},
                {"packages", QJsonArray{"act", "nosuch-thing"}},
                {"settings", QJsonArray{QJsonObject{{"name", "Dolphin"}, {"paths", QJsonArray{".config/dolphinrc"}},
                                                    {"secret", false}, {"bytes", 30}},
                                        QJsonObject{{"name", "SSH keys"}, {"paths", QJsonArray{".ssh"}},
                                                    {"secret", true}, {"bytes", 15}}}}};
    }
    // The wizard's choices for file (spec 4.6), keeping manifest.
    QString restoreChoices(const QString &file, const QJsonObject &manifest)
    {
        const QString choices = m_dir->filePath(QStringLiteral("restore-choices.json"));
        writeFile(choices, QJsonDocument(QJsonObject{{"file", file}, {"manifest", manifest}}).toJson());
        return choices;
    }
    Run worker(const QStringList &args)
    {
        QProcess p;
        p.setProcessEnvironment(m_env);
        p.start(QStringLiteral(GROUNDWORK_BIN), QStringList{QStringLiteral("--worker")} + args);
        return finish(p);
    }
    QStringList hintsFor(const Run &r, const QString &id) const
    {
        QStringList hints;
        for (const auto &m : r.markers)
            if (m.name == QLatin1String("HINT") && m.fields.value(0) == id)
                hints << m.fields.value(1);
        return hints;
    }

    // What the fake sfdisk was given on standard input.
    QByteArray tableWritten() const
    {
        QFile f(m_dir->filePath(QStringLiteral("table")));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    Run finish(QProcess &p)
    {
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

    // GRND-0092: a command's own line that looks like a marker is passed
    // on as text, never read as one.
    void aCommandsLineIsNeverAMarker()
    {
        writeFile(m_codes, "0\n0\n");
        m_env.insert(QStringLiteral("FAKE_ECHO"), QStringLiteral("@@STEP_END@@|forged|ok"));
        const Run r = run({QStringLiteral("system-update")});
        QCOMPARE(r.exitCode, 0);
        QVERIFY(r.endsFor(QStringLiteral("forged")).isEmpty());
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

    // GRND-0087: a step that fails on a nearly full disk says the disk is
    // full, not only an exit code; with room to spare it says nothing more.
    void aFullDiskIsSaid_data()
    {
        QTest::addColumn<QByteArray>("avail");
        QTest::addColumn<bool>("full");
        QTest::newRow("300 MiB free") << QByteArray("314572800") << true;
        QTest::newRow("20 GiB free") << QByteArray("21474836480") << false;
    }

    void aFullDiskIsSaid()
    {
        QFETCH(QByteArray, avail);
        QFETCH(bool, full);
        writeFile(m_bin + QStringLiteral("/df"),
                  "#!/bin/sh\necho \"df $*\" >> \"$FAKE_LOG\"\necho '     Avail Mounted on'\necho '" + avail
                      + " /'\necho '21474836480 /home'\n",
                  true);
        writeFile(m_codes, "0\n8\n"); // refresh ok, dup fails
        const Run r = run({QStringLiteral("system-update")});
        const QStringList end = r.endsFor(QStringLiteral("system-update"));
        QCOMPARE(end.value(1), QStringLiteral("fail"));
        QVERIFY2(end.value(2).contains(QStringLiteral("disk")) == full, qPrintable(end.value(2)));
        QCOMPARE(r.count(QStringLiteral("df ")), 1);
    }

    // GRND-0099: a dependency left out of the run blocks unless its check
    // reads already done or not needed here; "couldn't tell" blocks too.
    void aDependencyLeftOutThatCannotTellBlocks()
    {
        m_env.insert(QStringLiteral("FAKE_LIST_CODE"), QStringLiteral("1"));
        const Run r = run({QStringLiteral("flathub")});
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
        // The update comes too, as the Wizard sends it: the item installs fwupd
        // when it is missing (design, The levels).
        const Run r = run({QStringLiteral("system-update"), QStringLiteral("firmware-updates")});
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

    // Sharing folders (GRND-0066), switched on already, with the shares file
    // holding one folder. The fake dd keeps what it is given.
    void shareFixture()
    {
        m_env.insert(QStringLiteral("FAKE_WRITTEN"), m_dir->filePath(QStringLiteral("written")));
        writeFile(m_bin + QStringLiteral("/systemctl"), R"(#!/bin/sh
echo "systemctl $*" >> "$FAKE_LOG"
case "$1" in is-enabled) echo enabled;; is-active) echo active;; esac
exit 0
)", true);
        writeFile(m_bin + QStringLiteral("/touch"), "#!/bin/sh\necho \"touch $*\" >> \"$FAKE_LOG\"\n", true);
        writeFile(m_bin + QStringLiteral("/firewall-cmd"), "#!/bin/sh\necho \"firewall-cmd $*\" >> \"$FAKE_LOG\"\n", true);
        writeFile(m_bin + QStringLiteral("/dd"), R"(#!/bin/sh
echo "dd $*" >> "$FAKE_LOG"
out=$(printf '%s' "$1" | sed 's|^of=||; s|/|_|g')
cat > "$FAKE_WRITTEN$out"
)", true);
        writeFile(m_root + QStringLiteral("/etc/samba/smb.conf"),
                  "[global]\n\n# Added by Groundwork: the folders it shares.\n"
                  "include = /etc/samba/groundwork-shares.conf\n");
        writeFile(m_root + QStringLiteral("/etc/samba/groundwork-shares.conf"),
                  "[global]\n   map to guest = bad user\n\n[old]\n   path = /mnt/old\n   read only = yes\n");
    }
    QByteArray writtenShares() const
    {
        QFile written(m_dir->filePath(QStringLiteral("written_etc_samba_groundwork-shares.conf")));
        return written.open(QIODevice::ReadOnly) ? written.readAll() : QByteArray();
    }

    // INV-8: a done item runs when the value differs from what is applied,
    // and is skipped when it matches or no value was given. An item that
    // does not report what is applied is skipped as before.
    void aChangedValueRunsADoneItem()
    {
        shareFixture();
        const QString same = QStringLiteral(R"(network-shares=[{"path":"/mnt/old","write":false}])");
        Run r = run({QStringLiteral("--set"), same, QStringLiteral("network-shares")});
        QCOMPARE(r.endsFor(QStringLiteral("network-shares")).value(1), QStringLiteral("skip"));
        QCOMPARE(r.count(QStringLiteral("sudo -n -- dd")), 0);

        r = run({QStringLiteral("network-shares")}); // no --set
        QCOMPARE(r.endsFor(QStringLiteral("network-shares")).value(1), QStringLiteral("skip"));

        const QString refused = QStringLiteral(R"(network-shares=[{"path":"/etc","write":false}])");
        r = run({QStringLiteral("--set"), refused, QStringLiteral("network-shares")});
        QCOMPARE(r.endsFor(QStringLiteral("network-shares")).value(1), QStringLiteral("skip"));
        QCOMPARE(r.count(QStringLiteral("sudo -n -- dd")), 0);

        const QString changed = QStringLiteral(R"(network-shares=[{"path":"/mnt/new","write":true}])");
        // With the update, as the Wizard sends it when Samba is not yet here.
        r = run({QStringLiteral("--set"), changed, QStringLiteral("system-update"), QStringLiteral("network-shares")});
        QCOMPARE(r.endsFor(QStringLiteral("network-shares")).value(1), QStringLiteral("ok"));
        QVERIFY(writtenShares().contains("path = /mnt/new\n"));
        QVERIFY(r.calls.contains(QStringLiteral("sudo -n -- systemctl restart smb nmb wsdd")));

        writeFile(m_bin + QStringLiteral("/hostnamectl"), "#!/bin/sh\necho \"hostnamectl $*\" >> \"$FAKE_LOG\"\necho lounge-pc\n", true);
        QFile::remove(m_log);
        r = run({QStringLiteral("--set"), QStringLiteral("computer-name=other-pc"), QStringLiteral("computer-name")});
        QCOMPARE(r.endsFor(QStringLiteral("computer-name")).value(1), QStringLiteral("skip"));
        QCOMPARE(r.count(QStringLiteral("sudo -n -- hostnamectl hostname other-pc")), 0);
    }

    // GRND-0111: with Samba and wsdd here, a changed list runs with the update
    // left out; with one missing, the update is still needed.
    void aChangedListNeedsNoUpdateOnceThePackagesAreHere()
    {
        shareFixture();
        const QString changed = QStringLiteral(R"(network-shares=[{"path":"/mnt/new","write":true}])");
        Run r = run({QStringLiteral("--set"), changed, QStringLiteral("network-shares")});
        QCOMPARE(r.endsFor(QStringLiteral("network-shares")).value(1), QStringLiteral("ok"));
        QVERIFY(writtenShares().contains("path = /mnt/new\n"));
        QCOMPARE(r.count(QStringLiteral("sudo -n -- zypper")), 0);

        writeFile(m_bin + QStringLiteral("/systemctl"), R"(#!/bin/sh
case "$2" in wsdd) echo not-found; exit 4;; esac
case "$1" in is-enabled) echo enabled;; is-active) echo active;; esac
exit 0
)", true);
        r = run({QStringLiteral("--set"), changed, QStringLiteral("network-shares")});
        QCOMPARE(r.endsFor(QStringLiteral("network-shares")).value(1), QStringLiteral("skip"));
        QCOMPARE(r.count(QStringLiteral("sudo -n -- zypper")), 0);
    }

    // INV-9: spaces, an equals sign and a letter beyond ASCII survive the
    // trip through --set.
    void aListValueReachesItsItemWhole()
    {
        shareFixture();
        const QString path = QStringLiteral("/mnt/Spiele für alle=1");
        const QString given = QStringLiteral(R"(network-shares=[{"path":")") + path + QStringLiteral(R"(","write":false}])");
        const Run r = run({QStringLiteral("--set"), given, QStringLiteral("system-update"), QStringLiteral("network-shares")});
        QCOMPARE(r.endsFor(QStringLiteral("network-shares")).value(1), QStringLiteral("ok"));
        QVERIFY2(writtenShares().contains("   path = " + path.toUtf8() + '\n'), writtenShares().constData());
    }

    // A step's text reaches its command's standard input: how an item writes
    // a file as root with no shell (GRND-0052). The fake dd keeps what it
    // was given, named after its target.
    void aStepsTextReachesItsCommand()
    {
        const QString written = m_dir->filePath(QStringLiteral("written"));
        m_env.insert(QStringLiteral("FAKE_WRITTEN"), written);
        writeFile(m_bin + QStringLiteral("/systemctl"), R"(#!/bin/sh
echo "systemctl $*" >> "$FAKE_LOG"
case "$1" in is-enabled) echo disabled; exit 1;; is-active) echo inactive; exit 3;; esac
exit 0
)", true);
        writeFile(m_bin + QStringLiteral("/mkdir"), "#!/bin/sh\necho \"mkdir $*\" >> \"$FAKE_LOG\"\n", true);
        writeFile(m_bin + QStringLiteral("/dd"), R"(#!/bin/sh
echo "dd $*" >> "$FAKE_LOG"
out=$(printf '%s' "$1" | sed 's|^of=||; s|/|_|g')
cat > "$FAKE_WRITTEN$out"
)", true);
        // With the update, as the Wizard sends it (design, The levels).
        const Run r = run({QStringLiteral("system-update"), QStringLiteral("memory-protection")});
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(r.endsFor(QStringLiteral("memory-protection")).value(1), QStringLiteral("ok"));
        QVERIFY(r.calls.contains(QStringLiteral(
            "sudo -n -- dd of=/etc/systemd/user/app.slice.d/50-groundwork-memory.conf status=none")));
        QFile apps(written + QStringLiteral("_etc_systemd_user_app.slice.d_50-groundwork-memory.conf"));
        QVERIFY(apps.open(QIODevice::ReadOnly));
        QVERIFY(apps.readAll().contains("ManagedOOMMemoryPressure=kill\n"));
        QVERIFY(r.calls.contains(QStringLiteral("sudo -n -- systemctl enable --now systemd-oomd")));
    }

    // GRND-0076: a whole drive is written, then each partition formatted,
    // and the drive reads back as planned.
    void preparesAWholeDrive()
    {
        setUpDrive(wholeDriveLayout(), lsblkJson(kDriveSize, preparedParts()));
        const Run r = prepare();
        QCOMPARE(r.endsFor(QStringLiteral("prepare-drive")).value(1), QStringLiteral("ok"));
        QCOMPARE(r.exitCode, 0);
        QVERIFY(r.calls.contains(QStringLiteral("sudo -n -- sfdisk --wipe always --wipe-partitions always /dev/sdb")));
        QVERIFY(tableWritten().startsWith("label: gpt\n"));
        QVERIFY(r.calls.contains(QStringLiteral("sudo -n -- mkfs.fat -F 32 -n EFI /dev/sdb1")));
        QVERIFY(r.calls.contains(QStringLiteral("sudo -n -- mkswap -L swap /dev/sdb2")));
        QVERIFY(r.calls.contains(QStringLiteral("sudo -n -- mkfs.btrfs -f -L openSUSE /dev/sdb3")));
        // Spec 4.6: then what to pick in the installer, partition by partition.
        QStringList hints;
        for (const auto &m : r.markers)
            if (m.name == QLatin1String("HINT"))
                hints << m.fields.value(1);
        const QString said = hints.join(QLatin1Char('\n'));
        QVERIFY2(said.contains(QLatin1String("/dev/sdb1")) && said.contains(QLatin1String("/boot/efi")), qPrintable(said));
        QVERIFY2(said.contains(QLatin1String("/dev/sdb2")) && said.contains(QLatin1String("swap")), qPrintable(said));
        // The VM run: formatting the start-up partition in the installer
        // made it a Windows data partition, and the boot menu could not be
        // written.
        const QStringList startUp = hints.filter(QStringLiteral("/dev/sdb1"));
        QVERIFY2(startUp.size() == 1 && startUp.first().contains(QLatin1String("do not format")), qPrintable(said));
        // Formatting the system there leaves snapshots off unless asked.
        const QStringList system = hints.filter(QStringLiteral("/dev/sdb3"));
        QVERIFY2(system.size() == 1 && system.first().contains(QLatin1String("system: mount it at /"))
                     && system.first().contains(QLatin1String("snapshots")),
                 qPrintable(said));
        QVERIFY2(said.contains(QLatin1String("installer")), qPrintable(said));
    }

    // INV-3: a drive at the same path with another serial is not the drive
    // the user saw; nothing is written and no password is asked.
    void refusesADriveThatIsNotTheOneChosen()
    {
        QJsonObject layout = wholeDriveLayout();
        QJsonObject drive = layout.value("drive").toObject();
        drive.insert("serial", "S999");
        layout.insert("drive", drive);
        setUpDrive(layout, lsblkJson(kDriveSize, preparedParts()));
        const Run r = prepare();
        QVERIFY(r.count(QStringLiteral("lsblk")) > 0); // the drive was read
        QCOMPARE(r.endsFor(QStringLiteral("prepare-drive")).value(1), QStringLiteral("fail"));
        QCOMPARE(r.exitCode, 1);
        QCOMPARE(r.count(QStringLiteral("sudo")), 0);
        QVERIFY(!QFile::exists(m_dir->filePath(QStringLiteral("table"))));
    }

    // INV-4: the Worker checks the layout itself; two system partitions
    // are refused before any root step.
    void refusesAnInvalidLayout()
    {
        QJsonObject layout = wholeDriveLayout();
        layout.insert("partitions", QJsonArray{QJsonObject{{"use", "efi"}, {"size", double(4 * kGiB)}},
                                               QJsonObject{{"use", "system"}, {"size", double(10 * kGiB)}},
                                               QJsonObject{{"use", "system"}, {"size", QJsonValue()}}});
        setUpDrive(layout, lsblkJson(kDriveSize, preparedParts()));
        const Run r = prepare();
        QVERIFY(r.count(QStringLiteral("lsblk")) > 0);
        QCOMPARE(r.endsFor(QStringLiteral("prepare-drive")).value(1), QStringLiteral("fail"));
        QCOMPARE(r.exitCode, 1);
        QCOMPARE(r.count(QStringLiteral("sudo")), 0);
        QVERIFY(!QFile::exists(m_dir->filePath(QStringLiteral("table"))));
    }

    // INV-6: a partition missing after writing fails the run.
    void aMissingPartitionFails()
    {
        setUpDrive(wholeDriveLayout(), lsblkJson(kDriveSize, preparedParts().mid(0, 2)));
        const Run r = prepare();
        QVERIFY(r.calls.contains(QStringLiteral("sudo -n -- sfdisk --wipe always --wipe-partitions always /dev/sdb")));
        QCOMPARE(r.endsFor(QStringLiteral("prepare-drive")).value(1), QStringLiteral("fail"));
        QCOMPARE(r.exitCode, 1);
    }

    // INV-6: a format that reports success but leaves no filesystem fails
    // the run when the drive is read back.
    void aFormatThatLeftNoFilesystemFails()
    {
        QList<FakePart> parts = preparedParts();
        parts[1].fstype.clear();
        setUpDrive(wholeDriveLayout(), lsblkJson(kDriveSize, parts));
        const Run r = prepare();
        QVERIFY(r.calls.contains(QStringLiteral("sudo -n -- mkswap -L swap /dev/sdb2")));
        QCOMPARE(r.endsFor(QStringLiteral("prepare-drive")).value(1), QStringLiteral("fail"));
        QCOMPARE(r.exitCode, 1);
    }

    // GRND-0078, spec 4.5: the settings and the manifest are written to one
    // file at the output, as the user, through a .partial file.
    void savesASetupToOneFile()
    {
        QVERIFY(QDir().mkpath(m_dir->filePath(QStringLiteral("stick"))));
        const QString output = m_dir->filePath(QStringLiteral("stick/groundwork-setup-pc-2026-10-19.tar"));
        const Run r = save(saveSetup({".config/dolphinrc", ".ssh"}, output));
        QCOMPARE(r.endsFor(QStringLiteral("save-setup")).value(1), QStringLiteral("ok"));
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(r.count(QStringLiteral("sudo")), 0);
        QVERIFY(r.count(QStringLiteral("tar --create")) == 1 && r.count(QStringLiteral("tar --list")) == 1);
        QVERIFY(QFile::exists(output));
        QVERIFY(!QFile::exists(output + QStringLiteral(".partial")));
        QProcess list;
        list.start(QStringLiteral("/usr/bin/tar"), {QStringLiteral("--list"), QStringLiteral("--file"), output});
        list.waitForFinished();
        const QStringList members = QString::fromUtf8(list.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(members.value(0), QStringLiteral("groundwork-setup.json"));
        QVERIFY(members.contains(QStringLiteral(".config/dolphinrc")));
        QVERIFY(members.contains(QStringLiteral(".ssh/id_ed25519")));
    }

    // INV-3: a settings path outside the home folder is refused before any
    // step; tar is never run.
    void refusesASettingOutsideTheHomeFolder()
    {
        const QString output = m_dir->filePath(QStringLiteral("out.tar"));
        const Run r = save(saveSetup({"../x"}, output));
        QCOMPARE(r.endsFor(QStringLiteral("save-setup")).value(1), QStringLiteral("fail"));
        QCOMPARE(r.exitCode, 1);
        QCOMPARE(r.count(QStringLiteral("tar")), 0);
        QVERIFY(!QFile::exists(output));
    }

    // INV-5: when tar fails, nothing is left at the output.
    void aFailedWriteLeavesNoFile()
    {
        m_env.insert(QStringLiteral("FAKE_TAR_FAIL"), QStringLiteral("1"));
        const QString output = m_dir->filePath(QStringLiteral("out.tar"));
        const Run r = save(saveSetup({".config/dolphinrc"}, output));
        QCOMPARE(r.count(QStringLiteral("tar --create")), 1);
        QCOMPARE(r.endsFor(QStringLiteral("save-setup")).value(1), QStringLiteral("fail"));
        QCOMPARE(r.exitCode, 1);
        QVERIFY(!QFile::exists(output));
    }

    // GRND-0085: a file under a setting that cannot be read is left out;
    // the save still ends ok, and one hint names the setting.
    void anUnreadableFileIsLeftOut()
    {
        if (getuid() == 0)
            QSKIP("root reads every file");
        const QString output = m_dir->filePath(QStringLiteral("out.tar"));
        const QString choices = saveSetup({".config/dolphinrc", ".ssh"}, output);
        const QString locked = m_dir->filePath(QStringLiteral(".ssh/locked"));
        writeFile(locked, "owned by root, say\n");
        QVERIFY(QFile::setPermissions(locked, {}));
        const Run r = save(choices);
        QCOMPARE(r.endsFor(QStringLiteral("save-setup")).value(1), QStringLiteral("ok"));
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(std::count_if(r.markers.cbegin(), r.markers.cend(),
                               [](const gw::Marker &m) {
                                   return m.name == QLatin1String("HINT") && m.fields.value(1).contains(QLatin1String("Mine"));
                               }),
                 1);
        QProcess list;
        list.start(QStringLiteral("/usr/bin/tar"), {QStringLiteral("--list"), QStringLiteral("--file"), output});
        list.waitForFinished();
        const QStringList members = QString::fromUtf8(list.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVERIFY(members.contains(QStringLiteral(".ssh/id_ed25519")));
        QVERIFY(!members.contains(QStringLiteral(".ssh/locked")));
    }

    // GRND-0085: a settings path that is itself unreadable, a root-owned
    // ~/.viminfo say, gets no member from tar. It is left out of the saved
    // manifest too, so the save ends ok and the file reads back.
    void anUnreadableSettingsPathIsLeftOut()
    {
        if (getuid() == 0)
            QSKIP("root reads every file");
        const QString output = m_dir->filePath(QStringLiteral("out.tar"));
        const QString choices = saveSetup({".config/dolphinrc", ".viminfo"}, output);
        const QString locked = m_dir->filePath(QStringLiteral(".viminfo"));
        writeFile(locked, "owned by root, say\n");
        QVERIFY(QFile::setPermissions(locked, {}));
        const Run r = save(choices);
        QFile::setPermissions(locked, QFile::ReadOwner | QFile::WriteOwner);
        QCOMPARE(r.endsFor(QStringLiteral("save-setup")).value(1), QStringLiteral("ok"));
        QCOMPARE(std::count_if(r.markers.cbegin(), r.markers.cend(),
                               [](const gw::Marker &m) {
                                   return m.name == QLatin1String("HINT") && m.fields.value(1).contains(QLatin1String("Mine"));
                               }),
                 1);
        QProcess extract;
        extract.start(QStringLiteral("/usr/bin/tar"), {QStringLiteral("--extract"), QStringLiteral("--to-stdout"),
                                                       QStringLiteral("--file"), output, QStringLiteral("groundwork-setup.json")});
        extract.waitForFinished();
        const QJsonArray settings = QJsonDocument::fromJson(extract.readAllStandardOutput()).object().value("settings").toArray();
        QCOMPARE(settings.size(), 1);
        QCOMPARE(settings.first().toObject().value("paths").toArray(), QJsonArray{".config/dolphinrc"});
    }

    void refusesAnUnknownItem()
    {
        QCOMPARE(run({QStringLiteral("no-such-item")}).exitCode, 2);
    }

    // GRND-0079, INV-2: an entry the choices name that the saved file
    // lacks refuses the run before any step.
    void anEntryTheSavedFileLacksRefusesTheRun()
    {
        const QString file = savedFile();
        QVERIFY(!file.isEmpty());
        QJsonObject chosen = savedManifest();
        chosen.insert("packages", QJsonArray{"act", "evil"});
        const Run r = worker({QStringLiteral("--setup-choices"), restoreChoices(file, chosen),
                              QStringLiteral("carried-packages")});
        QCOMPARE(r.exitCode, 2);
        QCOMPARE(r.markers.value(0).name, QStringLiteral("SETUP_REFUSED"));
        QVERIFY(r.markers.value(0).fields.value(0).contains(QLatin1String("evil")));
        QCOMPARE(r.count(QStringLiteral("zypper")), 0);
        QCOMPARE(r.count(QStringLiteral("sudo")), 0);
    }

    // GRND-0088, INV-11: a Flathub app saved for the user may be restored
    // for everyone; the same change to an app from another remote refuses.
    void aFlathubUserAppMayBeRestoredForEveryone()
    {
        QJsonObject saved = savedManifest();
        saved.insert("flatpaks", QJsonArray{QJsonObject{{"app", "org.kde.kate"}, {"remote", "flathub"},
                                                        {"installation", "user"}},
                                            QJsonObject{{"app", "org.kde.krita"}, {"remote", "kdeapps"},
                                                        {"installation", "user"}}});
        const QString file = savedFile(saved);
        QVERIFY(!file.isEmpty());
        QJsonObject chosen = saved;
        chosen.remove("packages");
        chosen.remove("settings");
        chosen.insert("flatpaks", QJsonArray{QJsonObject{{"app", "org.kde.kate"}, {"remote", "flathub"},
                                                         {"installation", "system"}}});
        const Run r = worker({QStringLiteral("--setup-choices"), restoreChoices(file, chosen),
                              QStringLiteral("system-update"), QStringLiteral("flathub"),
                              QStringLiteral("carried-flatpaks")});
        QVERIFY2(r.markers.value(0).name != QLatin1String("SETUP_REFUSED"), qPrintable(r.markers.value(0).fields.join('|')));
        QVERIFY2(r.calls.contains(
                     QStringLiteral("sudo -n -- flatpak install -y --noninteractive --system flathub org.kde.kate")),
                 qPrintable(r.calls.join(QLatin1Char('\n'))));
        QCOMPARE(r.count(QStringLiteral("flatpak remote-add --user")), 0);

        chosen.insert("flatpaks", QJsonArray{QJsonObject{{"app", "org.kde.krita"}, {"remote", "kdeapps"},
                                                         {"installation", "system"}}});
        QFile::remove(m_log); // the first run's calls
        const Run refused = worker({QStringLiteral("--setup-choices"), restoreChoices(file, chosen),
                                    QStringLiteral("carried-flatpaks")});
        QCOMPARE(refused.exitCode, 2);
        QCOMPARE(refused.markers.value(0).name, QStringLiteral("SETUP_REFUSED"));
        QCOMPARE(refused.count(QStringLiteral("flatpak install")), 0);
    }

    // GRND-0079, INV-3: a carried id is accepted only from a saved file.
    void aCarriedIdNeedsASavedFile()
    {
        const Run r = run({QStringLiteral("carried-packages")});
        QCOMPARE(r.exitCode, 2);
        QCOMPARE(r.markers.value(0).name, QStringLiteral("UNKNOWN_ITEM"));
    }

    // GRND-0079, INV-4: a program the system lacks does not stop the others,
    // and is named after the run.
    void aProgramTheSystemLacksIsSkippedAndNamed()
    {
        writeFile(m_bin + QStringLiteral("/rpm"), kRpm, true);
        writeFile(m_bin + QStringLiteral("/zypper"), kZypperInstalls, true);
        m_env.insert(QStringLiteral("FAKE_INSTALLED"), m_dir->filePath(QStringLiteral("installed")));
        const QString file = savedFile();
        QJsonObject chosen = savedManifest();
        chosen.remove("settings");
        // The wizard pulls in the update, as every install needs it.
        const Run r = worker({QStringLiteral("--setup-choices"), restoreChoices(file, chosen),
                              QStringLiteral("system-update"), QStringLiteral("carried-packages")});
        QVERIFY2(r.endsFor(QStringLiteral("carried-packages")).value(1) == QLatin1String("ok"),
                 qPrintable(r.endsFor(QStringLiteral("carried-packages")).join(QLatin1Char('|'))));
        QCOMPARE(r.exitCode, 0);
        QVERIFY(r.calls.contains(QStringLiteral("sudo -n -- zypper -n --ignore-unknown install act nosuch-thing")));
        const QStringList hints = hintsFor(r, QStringLiteral("carried-packages"));
        QCOMPARE(hints.size(), 1);
        QVERIFY(hints.first().contains(QLatin1String("nosuch-thing")));
        QVERIFY(!hints.first().contains(QLatin1String("act")));
    }

    // GRND-0086: a program that clashes with this system costs only itself.
    // zypper -n installs nothing and exits 4 while one clashes, so the
    // Worker installs the rest in parts, and the run names the clash.
    void aClashingProgramCostsOnlyItself()
    {
        writeFile(m_bin + QStringLiteral("/rpm"), kRpm, true);
        writeFile(m_bin + QStringLiteral("/zypper"), kZypperClashes, true);
        m_env.insert(QStringLiteral("FAKE_INSTALLED"), m_dir->filePath(QStringLiteral("installed")));
        QJsonObject chosen = savedManifest();
        chosen.insert("packages", QJsonArray{"act", "clash-one", "hello", "nano", "vim", "nosuch-thing"});
        const QString file = savedFile(chosen);
        chosen.remove("settings");
        const Run r = worker({QStringLiteral("--setup-choices"), restoreChoices(file, chosen),
                              QStringLiteral("system-update"), QStringLiteral("carried-packages")});
        QVERIFY2(r.endsFor(QStringLiteral("carried-packages")).value(1) == QLatin1String("ok"),
                 qPrintable(r.endsFor(QStringLiteral("carried-packages")).join(QLatin1Char('|'))));
        QCOMPARE(r.exitCode, 0);
        QFile list(m_dir->filePath(QStringLiteral("installed")));
        QVERIFY(list.open(QIODevice::ReadOnly));
        const QStringList installed = QString::fromUtf8(list.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const char *name : {"act", "hello", "nano", "vim"})
            QVERIFY2(installed.contains(QLatin1String(name)), name);
        QVERIFY(!installed.contains(QStringLiteral("clash-one")));
        const QStringList hints = hintsFor(r, QStringLiteral("carried-packages"));
        QCOMPARE(hints.size(), 1);
        QVERIFY(hints.first().contains(QLatin1String("clash-one")));
        QVERIFY(hints.first().contains(QLatin1String("nosuch-thing")));
        QVERIFY(!hints.first().contains(QLatin1String("hello")));
    }

    // GRND-0092: a part that installs but skips an unreadable source says
    // so, as a whole install does.
    void aPartThatSkipsASourceSaysSo()
    {
        writeFile(m_bin + QStringLiteral("/rpm"), kRpm, true);
        writeFile(m_bin + QStringLiteral("/zypper"), kZypperClashesAndSkips, true);
        m_env.insert(QStringLiteral("FAKE_INSTALLED"), m_dir->filePath(QStringLiteral("installed")));
        QJsonObject chosen = savedManifest();
        chosen.insert("packages", QJsonArray{"act", "clash-one", "hello", "nano"});
        const QString file = savedFile(chosen);
        chosen.remove("settings");
        const Run r = worker({QStringLiteral("--setup-choices"), restoreChoices(file, chosen),
                              QStringLiteral("system-update"), QStringLiteral("carried-packages")});
        QCOMPARE(r.endsFor(QStringLiteral("carried-packages")).value(1), QStringLiteral("ok"));
        const QStringList hints = hintsFor(r, QStringLiteral("carried-packages"));
        QVERIFY2(std::any_of(hints.cbegin(), hints.cend(),
                             [](const QString &h) { return h.contains(QLatin1String("software source")); }),
                 qPrintable(hints.join(QLatin1Char('\n'))));
    }

    // GRND-0086: a failure that is not a clash, such as every part failing
    // the same way, stops after a few names fail alone rather than trying
    // each of hundreds.
    void aFailureThatIsNotAClashStopsSoon()
    {
        writeFile(m_bin + QStringLiteral("/rpm"), kRpm, true);
        writeFile(m_bin + QStringLiteral("/zypper"), kZypperInstallsNothing, true);
        m_env.insert(QStringLiteral("FAKE_INSTALLED"), m_dir->filePath(QStringLiteral("installed")));
        QStringList names;
        for (int i = 0; i < 300; ++i)
            names << QStringLiteral("pkg%1").arg(i);
        QJsonObject chosen = savedManifest();
        chosen.insert("packages", QJsonArray::fromStringList(names));
        const QString file = savedFile(chosen);
        chosen.remove("settings");
        const Run r = worker({QStringLiteral("--setup-choices"), restoreChoices(file, chosen),
                              QStringLiteral("system-update"), QStringLiteral("carried-packages")});
        QCOMPARE(r.endsFor(QStringLiteral("carried-packages")).value(1), QStringLiteral("fail"));
        QVERIFY2(r.count(QStringLiteral("zypper")) < 40, qPrintable(QString::number(r.count(QStringLiteral("zypper")))));
    }

    // GRND-0086: exit 4 and 8 also mean the system failed. When no part of
    // a short list installs, no name is taken to clash and the step fails,
    // where splitting down to single names would have ended it ok.
    void aShortListThatInstallsNothingFails()
    {
        writeFile(m_bin + QStringLiteral("/rpm"), kRpm, true);
        writeFile(m_bin + QStringLiteral("/zypper"), kZypperInstallsNothing, true);
        m_env.insert(QStringLiteral("FAKE_INSTALLED"), m_dir->filePath(QStringLiteral("installed")));
        QJsonObject chosen = savedManifest();
        chosen.insert("packages", QJsonArray{"act", "hello", "nano"});
        const QString file = savedFile(chosen);
        chosen.remove("settings");
        const Run r = worker({QStringLiteral("--setup-choices"), restoreChoices(file, chosen),
                              QStringLiteral("system-update"), QStringLiteral("carried-packages")});
        QCOMPARE(r.endsFor(QStringLiteral("carried-packages")).value(1), QStringLiteral("fail"));
    }

    // GRND-0079, INV-5: settings are copied as the user, into the home
    // folder, keeping the file they replace.
    void settingsAreRestoredAsTheUser()
    {
        const QString file = savedFile();
        writeFile(m_dir->filePath(QStringLiteral(".config/dolphinrc")), "the new install's own\n");
        QJsonObject chosen = savedManifest();
        chosen.remove("packages");
        const Run r = worker({QStringLiteral("--setup-choices"), restoreChoices(file, chosen),
                              QStringLiteral("carried-settings")});
        QCOMPARE(r.endsFor(QStringLiteral("carried-settings")).value(1), QStringLiteral("ok"));
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(r.count(QStringLiteral("tar --extract --file")), 1); // not the setup list's read
        QCOMPARE(r.count(QStringLiteral("sudo")), 0);
        QVERIFY(QFile::exists(m_dir->filePath(QStringLiteral(".config/dolphinrc.~1~"))));
        QFile restored(m_dir->filePath(QStringLiteral(".config/dolphinrc")));
        QVERIFY(restored.open(QIODevice::ReadOnly));
        QCOMPARE(restored.readAll(), QByteArray("[General]\nShowFullPath=true\n"));
        QVERIFY(!hintsFor(r, QStringLiteral("carried-settings")).isEmpty()); // log out and back in
    }

    // GRND-0079, INV-9: removing secrets leaves no member under a secret
    // setting; deleting removes the file.
    void secretsAreRemovedOrTheFileDeleted()
    {
        const QString file = savedFile();
        const Run removed = worker({QStringLiteral("--remove-secrets"), file});
        QCOMPARE(removed.endsFor(QStringLiteral("remove-secrets")).value(1), QStringLiteral("ok"));
        QCOMPARE(removed.exitCode, 0);
        QCOMPARE(removed.count(QStringLiteral("sudo")), 0);
        QProcess list;
        list.start(QStringLiteral("/usr/bin/tar"), {QStringLiteral("--list"), QStringLiteral("--file"), file});
        list.waitForFinished();
        const QString members = QString::fromUtf8(list.readAllStandardOutput());
        QVERIFY(!members.contains(QLatin1String(".ssh")));
        QVERIFY(members.contains(QLatin1String(".config/dolphinrc")));
        QVERIFY(members.contains(QLatin1String("groundwork-setup.json")));

        const Run deleted = worker({QStringLiteral("--delete-setup-file"), file});
        QCOMPARE(deleted.endsFor(QStringLiteral("delete-setup-file")).value(1), QStringLiteral("ok"));
        QVERIFY(!QFile::exists(file));
    }
};

QTEST_GUILESS_MAIN(TstWorker)
#include "tst_worker.moc"
