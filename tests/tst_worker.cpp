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
        const Run r = run({QStringLiteral("memory-protection")});
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

    void refusesAnUnknownItem()
    {
        QCOMPARE(run({QStringLiteral("no-such-item")}).exitCode, 2);
    }
};

QTEST_GUILESS_MAIN(TstWorker)
#include "tst_worker.moc"
