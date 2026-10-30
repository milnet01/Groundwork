// GRND-0073: system-wide settings in a saved setup. A fixture root and fake
// commands first on PATH, each recording its call, so these prove the
// commands Groundwork builds. Only the spec's recipe proves firewalld, CUPS
// and SELinux take the result (spec 7).
#include "carry/carry.h"
#include "carry/carrieditems.h"
#include "core/filereader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// Answers --list and --list --verbose from files the test writes.
const QByteArray kTar = R"(#!/bin/sh
echo "tar $*" >> "$FAKE_LOG"
case " $* " in
  *" --verbose "*) cat "$FAKE_TAR_VERBOSE";;
  *" --list "*) cat "$FAKE_TAR_LIST";;
  *" --to-stdout "*) cat "$FAKE_TAR_CONF";;
esac
)";

// Every unit is found and running, unless FAKE_NO_CUPS.
const QByteArray kSystemctl = R"(#!/bin/sh
echo "systemctl $*" >> "$FAKE_LOG"
case "$*" in
  *cups*) if [ -n "$FAKE_NO_CUPS" ]; then echo not-found; exit 4; fi;;
esac
case "$1" in
  is-enabled) echo enabled;;
  is-active) echo active;;
esac
)";

const QByteArray kRecord = R"(#!/bin/sh
echo "${0##*/} $*" >> "$FAKE_LOG"
)";

const QByteArray kLpstat = R"(#!/bin/sh
echo "lpstat $*" >> "$FAKE_LOG"
echo Office
echo Upstairs
)";

// The fstab of a default Tumbleweed install, with two drives of the owner's
// kind and one each of what must not travel.
const QByteArray kFstab =
    "UUID=aaaaaaaa-0000-0000-0000-000000000001  /                       btrfs  defaults                      0  0\n"
    "UUID=aaaaaaaa-0000-0000-0000-000000000001  /var                    btrfs  subvol=/@/var                 0  0\n"
    "UUID=aaaaaaaa-0000-0000-0000-000000000001  /usr/local              btrfs  subvol=/@/usr/local           0  0\n"
    "UUID=aaaaaaaa-0000-0000-0000-000000000001  /home                   btrfs  subvol=/@/home                0  0\n"
    "UUID=aaaaaaaa-0000-0000-0000-000000000001  /.snapshots             btrfs  subvol=/@/.snapshots          0  0\n"
    "UUID=bbbbbbbb-0000-0000-0000-000000000002  swap                    swap   defaults                      0  0\n"
    "UUID=CCCC-DDDD                             /boot/efi               vfat   utf8                          0  2\n"
    "tmpfs                                      /tmp                    tmpfs  size=1G                       0  0\n"
    "# UUID=dddddddd-0000-0000-0000-000000000003 /mnt/Old ext4 defaults 0 0\n"
    "UUID=eeeeeeee-0000-0000-0000-000000000004  /mnt/Games              ext4   nosuid,nodev,nofail,noatime,x-gvfs-show  0  0\n"
    "LABEL=Emulators                            /mnt/Emulators          xfs    defaults                      0  0\n"
    "UUID=ffffffff-0000-0000-0000-000000000005  /mnt/subvolume          btrfs  subvol=data                   0  0\n"
    "server:/export                             /mnt/nfs                nfs    defaults                      0  0\n"
    "UUID=abababab-0000-0000-0000-000000000006  /mnt/net                cifs   defaults                      0  0\n"
    "UUID=cdcdcdcd-0000-0000-0000-000000000007  /mnt/secret             ext4   defaults,password=hunter2     0  0\n"
    "UUID=efefefef-0000-0000-0000-000000000008  /mnt/creds              ext4   credentials=/root/c           0  0\n"
    "UUID=12121212-0000-0000-0000-000000000009  /opt/data               ext4   defaults                      0  0\n"
    "UUID=34343434-0000-0000-0000-00000000000a  /mnt/odd\\040name        ext4   defaults                      0  0\n";

gw::SettingsRow part(const QString &path, bool secret = false)
{
    return {path == gw::kFirewallPart ? QStringLiteral("Firewall") : QStringLiteral("Printers"), {path}, secret, 100};
}

class FakeItem : public gw::Item
{
public:
    explicit FakeItem(QString id) : m_id(std::move(id)) {}
    QString id() const override { return m_id; }
    gw::Level level() const override { return gw::Level::Essentials; }
    QString title() const override { return m_id; }
    QString applySentence() const override { return m_id; }
    gw::CheckResult check(const gw::CheckContext &) const override { return {}; }
    QList<gw::Step> applySteps(const gw::SystemIdentity &, const gw::CheckContext &) const override { return {}; }

private:
    QString m_id;
};

const FakeItem firewallRow(QStringLiteral("firewall"));
const FakeItem updateRow(QStringLiteral("system-update"));
const gw::Catalogue catalogue({&updateRow, &firewallRow});

} // namespace

class TstCarrySystem : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_bin, m_root, m_log, m_path;

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
    QStringList calls() const
    {
        QFile log(m_log);
        if (!log.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    // What a fake tar says the saved file holds.
    void tarLists(const QList<QPair<char, QString>> &members)
    {
        QByteArray list, verbose;
        for (const auto &[type, name] : members) {
            list += name.toUtf8() + '\n';
            verbose += QByteArray(1, type) + "rw-r--r-- root/root 10 2026-10-30 10:00 " + name.toUtf8() + '\n';
        }
        writeFile(m_dir->filePath(QStringLiteral("tar-list")), list);
        writeFile(m_dir->filePath(QStringLiteral("tar-verbose")), verbose);
    }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root)); }
    static const gw::Item *itemIn(const gw::CarriedItems &carried, const QString &id)
    {
        for (const std::unique_ptr<gw::Item> &item : carried.owned)
            if (item->id() == id)
                return item.get();
        return nullptr;
    }
    QList<gw::Step> stepsOf(const QString &part, bool secret = false)
    {
        gw::Manifest kept;
        kept.systemParts = {::part(part, secret)};
        const gw::CarriedItems carried = gw::carriedItems(kept, QStringLiteral("/data/saved.tar"), catalogue);
        const gw::Item *item = itemIn(carried, part == gw::kFirewallPart ? gw::kCarriedFirewallId : gw::kCarriedPrintersId);
        return item ? item->applySteps({}, context()) : QList<gw::Step>{};
    }
    static qsizetype stepIndex(const QList<gw::Step> &steps, const QString &starts)
    {
        for (qsizetype i = 0; i < steps.size(); ++i)
            if (steps[i].argv.join(QLatin1Char(' ')).startsWith(starts))
                return i;
        return -1;
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_bin = m_dir->filePath(QStringLiteral("bin"));
        m_root = m_dir->filePath(QStringLiteral("root"));
        m_log = m_dir->filePath(QStringLiteral("calls.log"));
        writeFile(m_bin + QStringLiteral("/tar"), kTar, true);
        writeFile(m_bin + QStringLiteral("/systemctl"), kSystemctl, true);
        writeFile(m_bin + QStringLiteral("/lpstat"), kLpstat, true);
        for (const char *tool : {"sudo", "firewall-cmd", "pkexec"})
            writeFile(m_bin + QLatin1Char('/') + QLatin1String(tool), kRecord, true);
        writeFile(m_root + QStringLiteral("/etc/fstab"), kFstab);
        m_path = qEnvironmentVariable("PATH");
        qputenv("PATH", (m_bin + QStringLiteral(":/usr/bin:/bin")).toUtf8());
        qputenv("FAKE_LOG", m_log.toUtf8());
        qputenv("FAKE_TAR_LIST", m_dir->filePath(QStringLiteral("tar-list")).toUtf8());
        qputenv("FAKE_TAR_VERBOSE", m_dir->filePath(QStringLiteral("tar-verbose")).toUtf8());
        qputenv("FAKE_TAR_CONF", m_dir->filePath(QStringLiteral("firewalld.conf")).toUtf8());
        writeFile(m_dir->filePath(QStringLiteral("firewalld.conf")), "# a comment\nDefaultZone=home\n");
        qunsetenv("FAKE_NO_CUPS");
        tarLists({{'-', QStringLiteral(".groundwork-system/firewall/firewalld.conf")},
                  {'-', QStringLiteral(".groundwork-system/firewall/zones/home.xml")},
                  {'-', QStringLiteral(".groundwork-system/printers/printers.conf")},
                  {'-', QStringLiteral(".groundwork-system/printers/ppd/Office.ppd")}});
    }
    void cleanup() { qputenv("PATH", m_path.toUtf8()); }

    // Spec 4.3's table.
    void mountOptions_data()
    {
        QTest::addColumn<QString>("options");
        QTest::addColumn<bool>("ok");
        QTest::newRow("the owner's") << "nosuid,nodev,nofail,noatime,x-gvfs-show" << true;
        QTest::newRow("with a value") << "defaults,uid=1000" << true;
        QTest::newRow("a space") << "a b" << false;
        QTest::newRow("a semicolon") << "defaults;x" << false;
        QTest::newRow("a password") << "defaults,password=x" << false;
        QTest::newRow("credentials") << "credentials=/root/c" << false;
        QTest::newRow("a user name") << "user=someone" << false;
    }
    void mountOptions()
    {
        QFETCH(QString, options);
        QFETCH(bool, ok);
        const gw::CarriedMount mount{QStringLiteral("UUID=1"), QStringLiteral("/mnt/a"), QStringLiteral("ext4"), options};
        QCOMPARE(gw::isCarriedMount(mount), ok);
    }

    // Spec 4.6's tables.
    void members_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("partPath");
        QTest::addColumn<bool>("ok");
        const QString fw = gw::kFirewallPart, pr = gw::kPrintersPart;
        QTest::newRow("a zone") << ".groundwork-system/firewall/zones/home.xml" << fw << true;
        QTest::newRow("firewalld.conf") << ".groundwork-system/firewall/firewalld.conf" << fw << true;
        QTest::newRow("a way out") << ".groundwork-system/firewall/zones/../../sudoers" << fw << false;
        QTest::newRow("the lockdown list") << ".groundwork-system/firewall/lockdown-whitelist.xml" << fw << false;
        QTest::newRow("firewalld's own copy") << ".groundwork-system/firewall/zones/home.xml.old" << fw << false;
        QTest::newRow("a ppd") << ".groundwork-system/printers/ppd/Office.ppd" << pr << true;
        QTest::newRow("printers.conf") << ".groundwork-system/printers/printers.conf" << pr << true;
        QTest::newRow("cupsd.conf") << ".groundwork-system/printers/cupsd.conf" << pr << false;
        QTest::newRow("the other part") << ".groundwork-system/firewall/zones/home.xml" << pr << false;
    }
    void members()
    {
        QFETCH(QString, name);
        QFETCH(QString, partPath);
        QFETCH(bool, ok);
        QCOMPARE(gw::isSystemMember(name, partPath), ok);
    }

    // What the spec's whole-folder save would have put in the file: firewalld
    // writes zones/home.xml.old beside a saved zone (its io/zone.py), so that
    // file would refuse every restore. Only names restoring takes are saved.
    void onlyWhatRestoringAcceptsIsSaved()
    {
        const auto firewall = gw::systemPartNamed(QStringLiteral("firewall"));
        const auto printers = gw::systemPartNamed(QStringLiteral("printers"));
        QVERIFY(firewall && printers);
        QVERIFY(!gw::systemPartNamed(QStringLiteral("sudoers")));
        QCOMPARE(gw::saveableSystemFiles(*firewall, {"firewalld.conf", "firewalld.conf.old", "lockdown-whitelist.xml",
                                                     "zones/home.xml", "zones/home.xml.old", "zones/home.xml.~1~",
                                                     "helpers/x.xml", "find: 'x': Permission denied"}),
                 QStringList({"firewalld.conf", "zones/home.xml"}));
        QCOMPARE(gw::saveableSystemFiles(*printers, {"printers.conf", "printers.conf.O", "classes.conf", "cupsd.conf",
                                                     "ppd/Office.ppd", "ppd/Office.ppd.O", "ssl/server.key",
                                                     "subscriptions.conf"}),
                 QStringList({"printers.conf", "classes.conf", "ppd/Office.ppd"}));
        QCOMPARE(firewall->source, QStringLiteral("/etc/firewalld"));
        QVERIFY(gw::findSystemFilesArguments(*firewall).contains(QStringLiteral("/etc/firewalld")));
        QVERIFY(!firewall->secret);
        QVERIFY(printers->secret);
    }

    // INV-1: gathering and checking run no firewall-cmd and no sudo.
    void nothingAsksForAPasswordBeforeSave()
    {
        const gw::CheckContext ctx = context();
        gw::Gathered found;
        gw::gatherSystem(ctx, &found);
        QVERIFY(found.firewallFound);
        QCOMPARE(found.printers, QStringList({"Office", "Upstairs"}));
        QCOMPARE(found.mounts.size(), 2);

        gw::Manifest kept;
        kept.systemParts = {part(gw::kFirewallPart), part(gw::kPrintersPart, true)};
        kept.mounts = found.mounts;
        const gw::CarriedItems carried = gw::carriedItems(kept, QStringLiteral("/data/saved.tar"), catalogue);
        QCOMPARE(int(carried.owned.size()), 3);
        for (const std::unique_ptr<gw::Item> &item : carried.owned)
            (void)item->check(ctx);
        const QStringList ran = calls();
        QVERIFY(!ran.isEmpty());
        for (const QString &call : ran) {
            QVERIFY2(!call.startsWith(QLatin1String("firewall-cmd")), qPrintable(call));
            QVERIFY2(!call.startsWith(QLatin1String("sudo")), qPrintable(call));
            QVERIFY2(!call.startsWith(QLatin1String("pkexec")), qPrintable(call));
        }
    }

    // INV-1: an unreadable or absent setting never reads as done.
    void aFirewallThatCannotBeReadIsNeverDone()
    {
        // No systemctl at all: the service manager cannot answer.
        QVERIFY(QFile::remove(m_bin + QStringLiteral("/systemctl")));
        qputenv("PATH", m_bin.toUtf8());
        gw::Manifest kept;
        kept.systemParts = {part(gw::kFirewallPart)};
        const gw::CarriedItems carried = gw::carriedItems(kept, QStringLiteral("/data/saved.tar"), catalogue);
        const gw::Item *item = itemIn(carried, gw::kCarriedFirewallId);
        QVERIFY(item);
        QVERIFY(item->dependsOn().contains(QStringLiteral("firewall")));
        QCOMPARE(item->check(context()).state, gw::CheckState::CouldNotTell);
    }

    // INV-3: the file's rows and members agree, through the real reader and
    // real tar, with the format still 1.
    void aFileWithSystemPartsReadsBack()
    {
        qputenv("PATH", "/usr/bin:/bin"); // the real tar
        const QString staged = m_dir->filePath(QStringLiteral("staged"));
        writeFile(staged + QStringLiteral("/.groundwork-system/firewall/zones/home.xml"), "<zone/>");
        writeFile(staged + QStringLiteral("/.groundwork-system/printers/printers.conf"), "<Printer Office>");
        gw::Manifest manifest;
        manifest.systemParts = {part(gw::kFirewallPart), part(gw::kPrintersPart, true)};
        manifest.mounts = {{QStringLiteral("UUID=1"), QStringLiteral("/mnt/a"), QStringLiteral("auto"), QStringLiteral("nofail")}};
        manifest.shares = {{QStringLiteral("/mnt/a/shared"), true}};
        const QJsonObject json = gw::manifestJson(manifest);
        QCOMPARE(json.value("format").toInt(), 1);
        // An older reader sees two settings rows.
        QCOMPARE(json.value("settings").toArray().size(), 2);
        writeFile(staged + QStringLiteral("/groundwork-setup.json"), QJsonDocument(json).toJson());
        const QString file = m_dir->filePath(QStringLiteral("saved.tar"));
        QCOMPARE(QProcess::execute(QStringLiteral("/usr/bin/tar"),
                                   {QStringLiteral("--create"), QStringLiteral("--format=posix"), QStringLiteral("--file"), file,
                                    QStringLiteral("-C"), staged, QStringLiteral("groundwork-setup.json"),
                                    QStringLiteral(".groundwork-system/firewall/zones/home.xml"),
                                    QStringLiteral(".groundwork-system/printers/printers.conf")}),
                 0);
        QString why;
        QStringList skipped;
        const auto read = gw::readSetupFile(context(), file, &why, &skipped);
        QVERIFY2(read, qPrintable(why));
        QVERIFY(skipped.isEmpty());
        QCOMPARE(read->systemParts.size(), 2);
        QVERIFY(read->settings.isEmpty());
        QCOMPARE(read->mounts, manifest.mounts);
        QCOMPARE(read->shares, manifest.shares);
        // A choices file may name only what the saved file holds.
        QVERIFY(gw::notInFile(*read, *read).isEmpty());
        gw::Manifest more = *read;
        more.mounts << gw::CarriedMount{QStringLiteral("UUID=2"), QStringLiteral("/mnt/b"), QStringLiteral("auto"), QStringLiteral("nofail")};
        QVERIFY(!gw::notInFile(more, *read).isEmpty());
        more = *read;
        more.shares << gw::CarriedShare{QStringLiteral("/mnt/a/other"), false};
        QVERIFY(!gw::notInFile(more, *read).isEmpty());
        more = *read;
        more.systemParts.removeLast();
        QVERIFY(gw::notInFile(more, *read).isEmpty());
        QVERIFY(!gw::notInFile(*read, more).isEmpty());
    }

    // A file cannot hide settings under the system parts' folder.
    void anotherPathUnderTheSystemFolderRefusesTheFile()
    {
        const QJsonObject json{{"format", 1},
                               {"settings", QJsonArray{QJsonObject{{"name", "Sneaky"},
                                                                   {"paths", QJsonArray{".groundwork-system/firewall/zones"}},
                                                                   {"secret", false},
                                                                   {"bytes", 1}}}}};
        QString why;
        QStringList skipped;
        QVERIFY(!gw::readManifest(json, &why, &skipped));
        QVERIFY(!why.isEmpty());
    }

    // Gathering never offers a home folder's copy from an older reader.
    void gatheringDoesNotOfferTheSystemFolder()
    {
        const QString home = m_dir->filePath(QStringLiteral("home"));
        writeFile(home + QStringLiteral("/.groundwork-system/firewall/zones/home.xml"), "<zone/>");
        writeFile(home + QStringLiteral("/.myapprc"), "x");
        const QList<gw::SettingsRow> rows = gw::gatherSettings(gw::CheckContext(gw::FileReader()), home);
        QVERIFY(!rows.isEmpty());
        for (const gw::SettingsRow &row : rows)
            QVERIFY2(!row.paths.join(QLatin1Char(' ')).contains(gw::kSystemPartsFolder), qPrintable(row.name));
    }

    // INV-4: each of these refuses the whole item before any step.
    void aTamperedMemberRefusesTheItem_data()
    {
        QTest::addColumn<char>("type");
        QTest::addColumn<QString>("name");
        QTest::newRow("a link") << 'l' << ".groundwork-system/firewall/zones/home.xml";
        QTest::newRow("a hard link") << 'h' << ".groundwork-system/firewall/zones/home.xml";
        QTest::newRow("a way out") << '-' << ".groundwork-system/firewall/zones/../../sudoers";
        QTest::newRow("a name not allowed") << '-' << ".groundwork-system/firewall/cupsd.conf";
        QTest::newRow("a device") << 'c' << ".groundwork-system/firewall/zones/null.xml";
    }
    void aTamperedMemberRefusesTheItem()
    {
        QFETCH(char, type);
        QFETCH(QString, name);
        tarLists({{'-', QStringLiteral(".groundwork-system/firewall/zones/good.xml")}, {type, name}});
        gw::Manifest kept;
        kept.systemParts = {part(gw::kFirewallPart)};
        const gw::CarriedItems carried = gw::carriedItems(kept, QStringLiteral("/data/saved.tar"), catalogue);
        const gw::Item *item = itemIn(carried, gw::kCarriedFirewallId);
        QVERIFY(item);
        const gw::CheckResult result = item->check(context());
        QCOMPARE(result.state, gw::CheckState::CouldNotTell);
        QVERIFY2(result.detail.contains(name), qPrintable(result.detail));
        QCOMPARE(item->applySteps({}, context()).size(), 0);
    }

    // INV-4: the two lists must describe the same members.
    void listsThatDisagreeRefuseTheItem()
    {
        writeFile(m_dir->filePath(QStringLiteral("tar-list")), ".groundwork-system/firewall/zones/a.xml\n");
        writeFile(m_dir->filePath(QStringLiteral("tar-verbose")), "");
        const gw::SystemMembers members = gw::systemMembers(context(), QStringLiteral("/data/saved.tar"), gw::kFirewallPart);
        QVERIFY(!members.problem.isEmpty());
        QVERIFY(members.files.isEmpty());
    }

    // Folders a file written by another tar may hold are not extracted.
    void folderMembersAreAcceptedAndNotExtracted()
    {
        tarLists({{'d', QStringLiteral(".groundwork-system/firewall/")},
                  {'d', QStringLiteral(".groundwork-system/firewall/zones/")},
                  {'-', QStringLiteral(".groundwork-system/firewall/zones/good.xml")}});
        const gw::SystemMembers members = gw::systemMembers(context(), QStringLiteral("/data/saved.tar"), gw::kFirewallPart);
        QVERIFY2(members.problem.isEmpty(), qPrintable(members.problem));
        QCOMPARE(members.files, QStringList{".groundwork-system/firewall/zones/good.xml"});
    }

    // INV-5, by gathering and by reading a file.
    void systemMountsAreNotCarried()
    {
        const QList<gw::CarriedMount> mounts = gw::carriedMounts(kFstab);
        QStringList points;
        for (const gw::CarriedMount &mount : mounts)
            points << mount.point;
        QCOMPARE(points, QStringList({"/mnt/Games", "/mnt/Emulators"}));
        QCOMPARE(mounts[0].source, QStringLiteral("UUID=eeeeeeee-0000-0000-0000-000000000004"));
        QCOMPARE(mounts[0].fstype, QStringLiteral("ext4"));
        QCOMPARE(mounts[0].options, QStringLiteral("nosuid,nodev,nofail,noatime,x-gvfs-show"));

        for (const char *point : {"/", "/home", "/home/someone/x", "/var", "/boot/efi", "/usr/local", "/etc/x", "/mnt/a b", "/mnt/a\nb",
                                  "/mnt/../etc", "relative"}) {
            const gw::CarriedMount bad{QStringLiteral("UUID=1"), QLatin1String(point), QStringLiteral("ext4"), QStringLiteral("nofail")};
            QVERIFY2(!gw::isCarriedMount(bad), point);
        }
        for (const char *source : {"/dev/sda1", "server:/export", "UUID=a b", "UUID=../../sda", "/dev/disk/by-uuid/../../sda"}) {
            const gw::CarriedMount bad{QLatin1String(source), QStringLiteral("/mnt/a"), QStringLiteral("ext4"), QStringLiteral("nofail")};
            QVERIFY2(!gw::isCarriedMount(bad), source);
        }
        for (const char *fstype : {"swap", "nfs", "nfs4", "cifs", "smb3", "sshfs", "fuse.sshfs", "ext4 defaults"}) {
            const gw::CarriedMount bad{QStringLiteral("UUID=1"), QStringLiteral("/mnt/a"), QLatin1String(fstype), QStringLiteral("nofail")};
            QVERIFY2(!gw::isCarriedMount(bad), fstype);
        }
        QVERIFY(gw::isCarriedMount({QStringLiteral("/dev/disk/by-label/Games"), QStringLiteral("/mnt/a"), QStringLiteral("auto"), {}}));

        const QJsonObject json{{"format", 1},
                               {"mounts", QJsonArray{QJsonObject{{"source", "UUID=1"}, {"point", "/"}, {"fstype", "ext4"}, {"options", "defaults"}},
                                                     QJsonObject{{"source", "UUID=2"}, {"point", "/mnt/ok"}, {"fstype", "ext4"}, {"options", "nofail"}}}}};
        QString why;
        QStringList skipped;
        const auto read = gw::readManifest(json, &why, &skipped);
        QVERIFY(read);
        QCOMPARE(read->mounts.size(), 1);
        QCOMPARE(read->mounts[0].point, QStringLiteral("/mnt/ok"));
        QCOMPARE(skipped.size(), 1);
    }

    // INV-6.
    void onlyPresentDrivesAreAdded()
    {
        const QString present = QStringLiteral("UUID=11111111-0000-0000-0000-000000000001");
        writeFile(m_root + QStringLiteral("/dev/disk/by-uuid/11111111-0000-0000-0000-000000000001"), "");
        writeFile(m_root + QStringLiteral("/dev/disk/by-label/Backups"), "");
        gw::Manifest kept;
        kept.mounts = {{present, QStringLiteral("/mnt/Here"), QStringLiteral("ext4"), QStringLiteral("noatime")},
                       {QStringLiteral("LABEL=Backups"), QStringLiteral("/mnt/Backups"), QStringLiteral("xfs"), QStringLiteral("nofail")},
                       {QStringLiteral("UUID=22222222-0000-0000-0000-000000000002"), QStringLiteral("/mnt/Absent"), QStringLiteral("ext4"), QStringLiteral("nofail")},
                       {QStringLiteral("UUID=eeeeeeee-0000-0000-0000-000000000004"), QStringLiteral("/mnt/SameDrive"), QStringLiteral("ext4"), QStringLiteral("nofail")},
                       {present, QStringLiteral("/mnt/Games"), QStringLiteral("ext4"), QStringLiteral("nofail")}};
        writeFile(m_root + QStringLiteral("/dev/disk/by-uuid/eeeeeeee-0000-0000-0000-000000000004"), "");
        const gw::CarriedItems carried = gw::carriedItems(kept, QStringLiteral("/data/saved.tar"), catalogue);
        const gw::Item *item = itemIn(carried, gw::kCarriedMountsId);
        QVERIFY(item);
        const gw::CheckResult before = item->check(context());
        QCOMPARE(before.state, gw::CheckState::NotDone);
        QVERIFY2(before.detail.contains(QLatin1String("/mnt/Absent")), qPrintable(before.detail));

        const QList<gw::Step> steps = item->applySteps({}, context());
        QCOMPARE(steps.size(), 5);
        QCOMPARE(steps[0].argv, QStringList({"mkdir", "-p", "/mnt/Here", "/mnt/Backups"}));
        QCOMPARE(steps[1].argv, QStringList({"dd", "of=/etc/fstab", "oflag=append", "conv=notrunc", "status=none"}));
        const QString lines = QString::fromUtf8(steps[1].input);
        QVERIFY2(lines.contains(present + QStringLiteral(" /mnt/Here ext4 noatime,nofail 0 0\n")), qPrintable(lines));
        QVERIFY2(lines.contains(QStringLiteral("LABEL=Backups /mnt/Backups xfs nofail 0 0\n")), qPrintable(lines));
        QVERIFY(!lines.contains(QLatin1String("Absent")));
        QVERIFY(!lines.contains(QLatin1String("SameDrive")));
        QVERIFY(!lines.contains(QLatin1String("/mnt/Games")));
        QCOMPARE(steps[2].argv, QStringList({"systemctl", "daemon-reload"}));
        QCOMPARE(steps[3].argv, QStringList({"mount", "/mnt/Here"}));
        QCOMPARE(steps[4].argv, QStringList({"mount", "/mnt/Backups"}));
        for (const gw::Step &step : steps) {
            QVERIFY(step.needsRoot);
            QVERIFY2(!step.argv.contains(QStringLiteral("chown")), "these drives hold files");
        }

        // A second run: the lines are there, so none is added again, and it
        // still names the drive that is absent.
        QFile fstab(m_root + QStringLiteral("/etc/fstab"));
        QVERIFY(fstab.open(QIODevice::Append));
        fstab.write(steps[1].input);
        fstab.close();
        const gw::CheckResult after = item->check(context());
        QCOMPARE(after.state, gw::CheckState::Done);
        QVERIFY(after.detail.contains(QLatin1String("/mnt/Absent")));
        QCOMPARE(item->applySteps({}, context()).size(), 0);
    }

    // INV-7.
    void sharesCarryNothingElse()
    {
        const QByteArray conf = "[global]\n   workgroup = HOME\n"
                                "[homes]\n   guest ok = yes\n   path = /home\n"
                                "[profiles]\n   path = /var/lib/samba/profiles\n"
                                "[Music]\n   path = /mnt/Games/Music\n   guest ok = yes\n   root preexec = /bin/evil\n"
                                "   read only = no\n"
                                "[Films]\n   path = /mnt/Games/Films\n   guest ok = Yes\n   writable = yes\n"
                                "[Books]\n   path = /mnt/Games/Books\n   guest ok = yes\n"
                                "[Private]\n   path = /mnt/Games/Private\n   valid users = someone\n"
                                "[System]\n   path = /etc\n   guest ok = yes\n";
        const gw::FoundShares found = gw::carriedShares(conf, QStringLiteral(R"([{"path":"/mnt/Games/PC Games","write":false}])"));
        QCOMPARE(found.shares, (QList<gw::CarriedShare>{{QStringLiteral("/mnt/Games/PC Games"), false},
                                                        {QStringLiteral("/mnt/Games/Music"), true},
                                                        {QStringLiteral("/mnt/Games/Films"), true},
                                                        {QStringLiteral("/mnt/Games/Books"), false}}));
        const QString left = found.notCarried.join(QLatin1Char('\n'));
        QVERIFY2(left.contains(QLatin1String("Private")), qPrintable(left));
        QVERIFY2(left.contains(QLatin1String("System")), qPrintable(left));
        QVERIFY2(!left.contains(QLatin1String("homes")), qPrintable(left));
        QVERIFY2(!left.contains(QLatin1String("profiles")), qPrintable(left)); // a stock smb.conf's own

        gw::Manifest manifest;
        manifest.shares = found.shares;
        const QByteArray json = QJsonDocument(gw::manifestJson(manifest)).toJson();
        QVERIFY(!json.contains("preexec"));
        QVERIFY(!json.contains("evil"));
        QVERIFY(!json.contains("Private"));

        // And a crafted file cannot bring a folder gathering refuses.
        const QJsonObject crafted{{"format", 1},
                                  {"shares", QJsonArray{QJsonObject{{"path", "/etc"}, {"write", true}},
                                                        QJsonObject{{"path", "/mnt/Games/Ok"}, {"write", true}, {"root preexec", "/bin/evil"}}}}};
        QString why;
        QStringList skipped;
        const auto read = gw::readManifest(crafted, &why, &skipped);
        QVERIFY(read);
        QCOMPARE(read->shares, (QList<gw::CarriedShare>{{QStringLiteral("/mnt/Games/Ok"), true}}));
        QCOMPARE(skipped.size(), 1);
    }

    // INV-8.
    void printersAreAlwaysSecret()
    {
        const QJsonObject crafted{{"format", 1},
                                  {"settings", QJsonArray{QJsonObject{{"name", "Printers"}, {"paths", QJsonArray{gw::kPrintersPart}},
                                                                      {"secret", false}, {"bytes", 1}},
                                                          QJsonObject{{"name", "Firewall"}, {"paths", QJsonArray{gw::kFirewallPart}},
                                                                      {"secret", false}, {"bytes", 1}}}}};
        QString why;
        QStringList skipped;
        const auto read = gw::readManifest(crafted, &why, &skipped);
        QVERIFY(read);
        QCOMPARE(read->systemParts.size(), 2);
        QVERIFY(read->systemParts[0].secret);
        QVERIFY(!read->systemParts[1].secret);
        QVERIFY(read->settings.isEmpty());
        const QStringList secret = gw::secretMembers({"groundwork-setup.json", ".groundwork-system/printers/printers.conf",
                                                      ".groundwork-system/firewall/zones/a.xml"},
                                                     *read);
        QCOMPARE(secret, QStringList{".groundwork-system/printers/printers.conf"});
        const gw::Manifest without = gw::withoutSecrets(*read);
        QCOMPARE(without.systemParts.size(), 1);
        QCOMPARE(without.systemParts[0].paths, QStringList{gw::kFirewallPart});
    }

    // INV-9.
    void restoreKeepsCopies()
    {
        const QList<gw::Step> steps = stepsOf(gw::kFirewallPart);
        const qsizetype extract = stepIndex(steps, QStringLiteral("tar --extract"));
        QVERIFY(extract >= 0);
        const gw::Step &tar = steps[extract];
        QVERIFY(tar.needsRoot);
        for (const char *option : {"--backup=numbered", "--no-same-owner", "--no-overwrite-dir", "--strip-components=2",
                                   "--verbatim-files-from", "--null", "--files-from=-"})
            QVERIFY2(tar.argv.contains(QLatin1String(option)), option);
        QVERIFY(tar.argv.join(QLatin1Char(' ')).contains(QLatin1String("-C /etc/firewalld")));
        // The named files on input: never firewalld.conf.
        QCOMPARE(tar.input, QByteArray(".groundwork-system/firewall/zones/home.xml") + '\0');
        const qsizetype mkdir = stepIndex(steps, QStringLiteral("mkdir -p"));
        const qsizetype reload = stepIndex(steps, QStringLiteral("firewall-cmd --reload"));
        const qsizetype zone = stepIndex(steps, QStringLiteral("firewall-cmd --set-default-zone=home"));
        QVERIFY(mkdir >= 0 && mkdir < extract && extract < reload && reload < zone);
        for (const gw::Step &step : steps)
            QVERIFY(step.needsRoot);
        QCOMPARE(stepIndex(steps, QStringLiteral("restorecon")), qsizetype(-1)); // no SELinux in the fixture
        writeFile(m_root + QStringLiteral("/sys/fs/selinux/enforce"), "1");
        const QList<gw::Step> selinux = stepsOf(gw::kFirewallPart);
        const qsizetype relabel = stepIndex(selinux, QStringLiteral("restorecon -R /etc/firewalld"));
        QVERIFY(relabel > stepIndex(selinux, QStringLiteral("tar --extract")));
        QVERIFY(relabel < stepIndex(selinux, QStringLiteral("firewall-cmd --reload")));
    }

    // Spec 4.7: a default zone that could be anything is not passed on.
    void aBadDefaultZoneIsLeftOut()
    {
        writeFile(m_dir->filePath(QStringLiteral("firewalld.conf")), "DefaultZone=home; rm -rf /\n");
        QCOMPARE(stepIndex(stepsOf(gw::kFirewallPart), QStringLiteral("firewall-cmd --set-default-zone")), qsizetype(-1));
    }

    // INV-10.
    void printerStepsStopCups()
    {
        const QList<gw::Step> steps = stepsOf(gw::kPrintersPart, true);
        QCOMPARE(stepIndex(steps, QStringLiteral("zypper")), qsizetype(-1)); // cups is here
        const qsizetype stop = stepIndex(steps, QStringLiteral("systemctl stop cups"));
        const qsizetype extract = stepIndex(steps, QStringLiteral("tar --extract"));
        const qsizetype owner = stepIndex(steps, QStringLiteral("chown root:lp /etc/cups/printers.conf"));
        const qsizetype mode = stepIndex(steps, QStringLiteral("chmod 600 /etc/cups/printers.conf"));
        const qsizetype start = stepIndex(steps, QStringLiteral("systemctl enable --now cups"));
        QVERIFY(stop >= 0 && stop < extract && extract < owner && owner < mode && mode < start);
        QVERIFY(steps[extract].argv.join(QLatin1Char(' ')).contains(QLatin1String("-C /etc/cups")));
        QVERIFY(steps[extract].argv.contains(QStringLiteral("--backup=numbered")));
        QVERIFY(steps[extract].input.contains(".groundwork-system/printers/ppd/Office.ppd"));
        QVERIFY(!steps[extract].input.contains("firewall"));
        for (const gw::Step &step : steps)
            QVERIFY(step.needsRoot);
    }

    // Spec 4.6: cups missing, the item installs it, so it waits for the update.
    void missingCupsIsInstalledAfterTheUpdate()
    {
        qputenv("FAKE_NO_CUPS", "1");
        gw::Manifest kept;
        kept.systemParts = {part(gw::kPrintersPart, true)};
        const gw::CarriedItems carried = gw::carriedItems(kept, QStringLiteral("/data/saved.tar"), catalogue);
        const gw::Item *item = itemIn(carried, gw::kCarriedPrintersId);
        QVERIFY(item);
        const gw::CheckResult missing = item->check(context());
        QCOMPARE(missing.state, gw::CheckState::NotDone);
        QVERIFY(item->installsPackages());
        QVERIFY(!missing.nothingToInstall);
        const QList<gw::Step> steps = item->applySteps({}, context());
        QCOMPARE(steps.first().argv, QStringList({"zypper", "-n", "install", "cups"}));
        QCOMPARE(steps.first().tool, gw::Step::Tool::Zypper);

        qunsetenv("FAKE_NO_CUPS");
        QVERIFY(item->check(context()).nothingToInstall);
    }

    // A kept part builds an item; nothing kept builds none.
    void nothingKeptBuildsNoItem()
    {
        QCOMPARE(int(gw::carriedItems({}, QStringLiteral("/data/saved.tar"), catalogue).owned.size()), 0);
        for (const QString &id : {gw::kCarriedFirewallId, gw::kCarriedPrintersId, gw::kCarriedMountsId})
            QVERIFY(gw::isCarriedId(id));
    }
};

QTEST_GUILESS_MAIN(TstCarrySystem)
#include "tst_carrysystem.moc"
