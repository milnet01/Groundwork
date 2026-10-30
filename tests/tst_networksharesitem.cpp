// Sharing folders on the network (GRND-0066), against a fixture root and fake
// commands on PATH. These prove the text and steps Groundwork produces; only
// the spec's manual recipe proves Samba, the firewall or SELinux obey them.
#include "core/folderlist.h"
#include "items/networksharesitem.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <pwd.h>
#include <unistd.h>

namespace {
const QString kShares = QStringLiteral("/etc/samba/groundwork-shares.conf");
const QString kConf = QStringLiteral("/etc/samba/smb.conf");
const QByteArray kInclude = "include = /etc/samba/groundwork-shares.conf";
const QByteArray kOwnersConf = "[global]\n   workgroup = WORKGROUP\n   map to guest = bad user\n\n"
                               "[printers]\n   path = /var/tmp\n\n"
                               "[ROMS]\n   path = /mnt/Games/ROMS\n   read only = yes\n\n"
                               "; [Old]\n# [Older]\n"
                               "[PC Games]\n   path = /mnt/Games/PC Games\n";

QString value(const QStringList &paths, bool write = false)
{
    QList<gw::ListedFolder> folders;
    for (const QString &path : paths)
        folders.append({path, write});
    return gw::folderListValue(folders);
}

// The headings in a shares file, each [name].
QStringList sections(const QByteArray &text)
{
    QStringList names;
    for (const QByteArray &line : text.split('\n'))
        if (line.startsWith('['))
            names << QString::fromUtf8(line);
    return names;
}
} // namespace

class TstNetworkSharesItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin;
    QByteArray m_oldPath;
    std::unique_ptr<QTemporaryDir> m_root;
    const gw::NetworkSharesItem m_item;

    void write(const QString &path, const QByteArray &text, bool executable = false)
    {
        QVERIFY(QDir().mkpath(QFileInfo(path).path()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
        if (executable)
            f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
    }
    void fixture(const QString &path, const QByteArray &text) { write(m_root->path() + path, text); }
    // systemctl answers from a list of the units found, enabled and active.
    // Each call is written to a log, to show what a check ran.
    void services(const QByteArray &running, const QByteArray &found = "smb nmb wsdd firewalld")
    {
        write(m_bin.filePath(QStringLiteral("systemctl")),
              "#!/bin/sh\necho \"systemctl $*\" >> '" + m_bin.filePath(QStringLiteral("calls")).toUtf8() + "'\n"
              "found=' " + found + " '; running=' " + running + " '\n"
              "case \"$found\" in *\" $2 \"*) ;; *) echo not-found; exit 4;; esac\n"
              "case \"$running\" in *\" $2 \"*) [ \"$1\" = is-enabled ] && echo enabled || echo active; exit 0;; esac\n"
              "[ \"$1\" = is-enabled ] && echo disabled || echo inactive; exit 1\n",
              true);
    }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }
    QList<gw::Step> steps(const QString &given) const { return m_item.applyStepsFor({}, context(), given); }
    // What the shares file would hold after applying the value.
    QByteArray written(const QString &given) const
    {
        const QString target = QStringLiteral("of=") + kShares;
        for (const gw::Step &step : steps(given))
            if (step.argv.value(1) == target)
                return step.input;
        return {};
    }

private slots:
    void initTestCase()
    {
        m_oldPath = qgetenv("PATH");
        qputenv("PATH", m_bin.path().toUtf8());
    }
    void cleanupTestCase() { qputenv("PATH", m_oldPath); }
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        QFile::remove(m_bin.filePath(QStringLiteral("calls")));
        fixture(kConf, kOwnersConf);
        services("smb nmb");
    }

    void aRefusedFolderRefusesTheValue_data()
    {
        QTest::addColumn<QString>("given");
        const QString home = QDir::homePath();
        auto path = [](const QString &p) { return value({p}); };
        QTest::newRow("relative") << path(QStringLiteral("mnt/a"));
        QTest::newRow("dot dot") << path(QStringLiteral("/mnt/a/../etc"));
        QTest::newRow("double slash") << path(QStringLiteral("/mnt//a"));
        QTest::newRow("trailing slash") << path(QStringLiteral("/mnt/a/"));
        QTest::newRow("newline") << path(QStringLiteral("/mnt/a\n[x]"));
        QTest::newRow("tab") << path(QStringLiteral("/mnt/a\tb"));
        QTest::newRow("percent") << path(QStringLiteral("/mnt/a%u"));
        QTest::newRow("quote") << path(QStringLiteral("/mnt/a\"b"));
        QTest::newRow("backslash") << path(QStringLiteral("/mnt/a\\"));
        QTest::newRow("leading space") << path(QStringLiteral(" /mnt/a"));
        QTest::newRow("trailing space") << path(QStringLiteral("/mnt/a "));
        QTest::newRow("two spaces") << path(QStringLiteral("/mnt/a  b"));
        QTest::newRow("root") << path(QStringLiteral("/"));
        for (const char *dir : {"/bin", "/boot", "/dev", "/etc", "/lib", "/lib64", "/opt", "/proc", "/root", "/sbin",
                                "/sys", "/usr", "/var"}) {
            QTest::newRow(dir) << path(QLatin1String(dir));
            QTest::newRow(qPrintable(QLatin1String(dir) + QStringLiteral("/below")))
                << path(QLatin1String(dir) + QStringLiteral("/below"));
        }
        QTest::newRow("run") << path(QStringLiteral("/run"));
        QTest::newRow("run user") << path(QStringLiteral("/run/user/1000"));
        QTest::newRow("home") << path(home);
        QTest::newRow("all homes") << path(QStringLiteral("/home"));
        QTest::newRow("hidden in home") << path(home + QStringLiteral("/.ssh"));
        QTest::newRow("below hidden") << path(home + QStringLiteral("/.config/app"));
        QTest::newRow("twice") << value({QStringLiteral("/mnt/a"), QStringLiteral("/mnt/a")});
        QStringList many;
        for (int i = 0; i <= gw::kMaxListedFolders; ++i)
            many << QStringLiteral("/mnt/d%1").arg(i);
        QTest::newRow("too many") << value(many);
        QTest::newRow("too long") << path(QStringLiteral("/mnt/") + QString(1024, QLatin1Char('a')));
        QTest::newRow("empty") << QString();
        QTest::newRow("not json") << QStringLiteral("/mnt/a");
        QTest::newRow("extra key") << QStringLiteral(R"([{"path":"/mnt/a","write":false,"x":1}])");
        QTest::newRow("write not a boolean") << QStringLiteral(R"([{"path":"/mnt/a","write":"no"}])");
    }
    void aRefusedFolderRefusesTheValue()
    {
        QFETCH(QString, given);
        QVERIFY2(!m_item.isValidValue(given), qPrintable(given));
    }

    void aGoodListIsAccepted_data()
    {
        QTest::addColumn<QString>("given");
        QTest::newRow("a space") << value({QStringLiteral("/mnt/Games/PC Games")});
        QTest::newRow("an equals sign") << value({QStringLiteral("/mnt/a=b")});
        QTest::newRow("a letter beyond ASCII") << value({QStringLiteral("/mnt/Spiele für alle")});
        QTest::newRow("a drive the desktop opened") << value({QStringLiteral("/run/media/ants/Stick")});
        QTest::newRow("in the home folder") << value({QDir::homePath() + QStringLiteral("/Music")});
        QTest::newRow("hidden outside the home folder") << value({QStringLiteral("/mnt/a/.b")});
        QTest::newRow("nothing") << QStringLiteral("[]");
        QTest::newRow("at the cap") << value([] {
            QStringList most;
            for (int i = 0; i < gw::kMaxListedFolders; ++i)
                most << QStringLiteral("/mnt/d%1").arg(i);
            return most;
        }());
    }
    void aGoodListIsAccepted()
    {
        QFETCH(QString, given);
        QVERIFY2(m_item.isValidValue(given), qPrintable(given));
    }

    // Each refusal has a sentence for the list to show.
    void aRefusedFolderSaysWhy()
    {
        QVERIFY(!m_item.whyRefused(QStringLiteral("/etc")).isEmpty());
        QVERIFY(!m_item.whyRefused(QDir::homePath()).isEmpty());
        QVERIFY(m_item.whyRefused(QStringLiteral("/mnt/Games/PC Games")).isEmpty());
    }

    // A link that leads to a refused folder is refused: Samba follows the
    // share's own path, whatever wide links says.
    void aLinkToARefusedFolderIsRefused()
    {
        QTemporaryDir dir;
        const QString link = dir.filePath(QStringLiteral("looks-harmless"));
        QVERIFY(QFile::link(QStringLiteral("/etc"), link));
        QVERIFY(!m_item.whyRefused(link).isEmpty());
        QVERIFY(!m_item.isValidValue(value({link})));
    }

    void folderNamesCannotAddSections_data()
    {
        QTest::addColumn<QString>("folder");
        QTest::newRow("closing bracket") << QStringLiteral("/mnt/a]b");
        QTest::newRow("a section") << QStringLiteral("/mnt/[global]");
        QTest::newRow("equals") << QStringLiteral("/mnt/path = x");
        QTest::newRow("ordinary") << QStringLiteral("/mnt/Games");
    }
    void folderNamesCannotAddSections()
    {
        QFETCH(QString, folder);
        const QByteArray text = written(value({folder}));
        QCOMPARE(text.count("\n["), 2); // [global] and the share, after the comment
        const QStringList heads = sections(text);
        QCOMPARE(heads.size(), 2);
        QCOMPARE(heads.first(), QStringLiteral("[global]"));
        static const QRegularExpression safe(QStringLiteral("^\\[[^\\[\\]=]+\\]$"));
        QVERIFY2(safe.match(heads.last()).hasMatch(), qPrintable(heads.last()));
        QVERIFY(text.contains("   path = " + folder.toUtf8() + '\n'));
    }

    void aNameInUseGetsANumber()
    {
        const QStringList names = gw::NetworkSharesItem::shareNames(
            {QStringLiteral("/mnt/roms"), QStringLiteral("/mnt/users"), QStringLiteral("/mnt/a/Games"),
             QStringLiteral("/mnt/b/games"), QStringLiteral("/mnt/..."), QStringLiteral("/mnt/") + QString(90, QLatin1Char('x'))},
            {QStringLiteral("ROMS")});
        QCOMPARE(names.value(0), QStringLiteral("roms 2"));
        QCOMPARE(names.value(1), QStringLiteral("users 2"));
        QCOMPARE(names.value(2), QStringLiteral("Games"));
        QCOMPARE(names.value(3), QStringLiteral("games 2"));
        QCOMPARE(names.value(4), QStringLiteral("Folder"));
        QCOMPARE(names.value(5), QString(64, QLatin1Char('x')));

        // The sections of smb.conf count, and a commented-out one does not.
        const QByteArray text = written(value({QStringLiteral("/mnt/x/roms"), QStringLiteral("/mnt/x/Old")}));
        QVERIFY(text.contains("[roms 2]\n"));
        QVERIFY(text.contains("[Old]\n"));
    }

    void theOwnersConfigIsOnlyAppendedTo()
    {
        const QString confTarget = QStringLiteral("of=") + kConf;
        const QList<gw::Step> first = steps(QStringLiteral("[]"));
        int appends = 0;
        for (const gw::Step &step : first) {
            if (step.argv.value(1) != confTarget)
                continue;
            ++appends;
            QVERIFY(step.argv.contains(QStringLiteral("oflag=append")));
            QVERIFY(step.argv.contains(QStringLiteral("conv=notrunc")));
            QCOMPARE(step.input, "\n# Added by Groundwork: the folders it shares.\n" + kInclude + '\n');
        }
        QCOMPARE(appends, 1);

        fixture(kConf, kOwnersConf + '\n' + kInclude + '\n');
        for (const gw::Step &step : steps(QStringLiteral("[]")))
            QVERIFY(step.argv.value(1) != confTarget);

        // A commented-out line is not the include.
        fixture(kConf, kOwnersConf + "# " + kInclude + '\n');
        appends = 0;
        for (const gw::Step &step : steps(QStringLiteral("[]")))
            appends += step.argv.value(1) == confTarget ? 1 : 0;
        QCOMPARE(appends, 1);
    }

    void eachShareSaysWhoAndWhat()
    {
        const passwd *me = ::getpwuid(::getuid());
        QVERIFY(me);
        const QByteArray user = me->pw_name;
        const QByteArray text = written(gw::folderListValue(
            {{QStringLiteral("/mnt/look"), false}, {QStringLiteral("/mnt/add"), true}}));
        QVERIFY(text.contains("map to guest = bad user\n"));
        const qsizetype split = text.indexOf("[add]");
        QVERIFY(split > 0);
        const QByteArray look = text.mid(text.indexOf("[look]"), split - text.indexOf("[look]"));
        const QByteArray add = text.mid(split);
        for (const QByteArray &share : {look, add}) {
            QVERIFY(share.contains("guest ok = yes\n"));
            QVERIFY(share.contains("force user = " + user + '\n'));
            QVERIFY(share.contains("browseable = yes\n"));
        }
        QVERIFY(look.contains("read only = yes\n"));
        QVERIFY(add.contains("read only = no\n"));
        QVERIFY(!text.contains("wide links"));
    }

    // What was written reads back as the same value, so the Worker can tell
    // when the list has changed.
    void aWrittenListReadsBack()
    {
        const QString given = gw::folderListValue({{QStringLiteral("/mnt/Games/PC Games"), false},
                                                   {QStringLiteral("/mnt/Spiele für alle"), true}});
        fixture(kShares, written(given));
        fixture(kConf, kOwnersConf + kInclude + '\n');
        services("smb nmb wsdd");
        const gw::CheckResult r = m_item.check(context());
        QCOMPARE(r.state, gw::CheckState::Done);
        QCOMPARE(r.currentValue, given);
    }

    void theCheckNeedsNoRoot()
    {
        // smb.conf cannot be read: applying fixes it, so it is never
        // "couldn't tell", which the Worker would skip for ever.
        QFile::remove(m_root->path() + kConf);
        services("smb nmb wsdd");
        fixture(kShares, written(QStringLiteral("[]")));
        QCOMPARE(m_item.check(context()).state, gw::CheckState::NotDone);

        QFile calls(m_bin.filePath(QStringLiteral("calls")));
        QVERIFY(calls.open(QIODevice::ReadOnly));
        const QByteArray asked = calls.readAll();
        QVERIFY(asked.contains("systemctl is-enabled smb"));
        QVERIFY(!asked.contains("firewall-cmd"));
        QVERIFY(!asked.contains("sudo"));

        fixture(kConf, kOwnersConf + kInclude + '\n');
        QFile::remove(m_root->path() + kShares);
        QCOMPARE(m_item.check(context()).state, gw::CheckState::NotDone);
        fixture(kShares, written(QStringLiteral("[]")));
        QCOMPARE(m_item.check(context()).state, gw::CheckState::Done);
        services("smb nmb");
        QCOMPARE(m_item.check(context()).state, gw::CheckState::NotDone);

        QFile::remove(m_bin.filePath(QStringLiteral("systemctl")));
        QCOMPARE(m_item.check(context()).state, gw::CheckState::CouldNotTell);
    }

    // The owner's other shares are named and offered to the list as taken;
    // Samba's own sections are not named.
    void theCheckNamesOtherShares()
    {
        const gw::CheckResult r = m_item.check(context());
        QVERIFY2(r.detail.contains(QLatin1String("ROMS")), qPrintable(r.detail));
        QVERIFY(r.detail.contains(QLatin1String("PC Games")));
        QVERIFY(!r.detail.contains(QLatin1String("printers")));
        QVERIFY(!r.detail.contains(QLatin1String("Old")));
        QCOMPARE(r.heldFolders.value(QStringLiteral("/mnt/Games/PC Games")), QStringLiteral("PC Games"));
    }

    void theRowSaysAnyoneCanGetIn()
    {
        const QString sentence = m_item.applySentence().toLower();
        QVERIFY(sentence.contains(QLatin1String("anyone")));
        QVERIFY(sentence.contains(QLatin1String("no password")));
        QVERIFY(sentence.contains(QLatin1String("any network")));
    }

    void theStepsRunInOrder()
    {
        services("smb nmb firewalld");
        QStringList said;
        for (const gw::Step &step : steps(value({QStringLiteral("/mnt/a")})))
            said << step.argv.join(QLatin1Char(' '));
        auto at = [&said](const QString &start) {
            for (qsizetype i = 0; i < said.size(); ++i)
                if (said[i].startsWith(start))
                    return i;
            return qsizetype(-1);
        };
        const qsizetype write = at(QStringLiteral("dd of=") + kShares);
        const qsizetype append = at(QStringLiteral("dd of=") + kConf);
        const qsizetype touch = at(QStringLiteral("touch ") + kConf);
        const qsizetype enable = at(QStringLiteral("systemctl enable smb nmb wsdd"));
        const qsizetype restart = at(QStringLiteral("systemctl restart smb nmb wsdd"));
        const qsizetype samba = at(QStringLiteral("firewall-cmd --permanent --add-service=samba"));
        const qsizetype wsdd = at(QStringLiteral("firewall-cmd --permanent --add-service=wsdd"));
        const qsizetype reload = at(QStringLiteral("firewall-cmd --reload"));
        QVERIFY2(write >= 0 && write < append && append < touch && touch < enable && enable < restart
                     && restart < samba && samba < wsdd && wsdd < reload,
                 qPrintable(said.join(QLatin1Char('\n'))));
        QCOMPARE(at(QStringLiteral("zypper")), qsizetype(-1)); // both found

        services("smb nmb");
        for (const gw::Step &step : steps(value({QStringLiteral("/mnt/a")})))
            QVERIFY(step.argv.value(0) != QLatin1String("firewall-cmd"));
    }

    // Only what is missing is installed, first of all.
    void onlyWhatIsMissingIsInstalled()
    {
        services("", "smb nmb firewalld");
        QList<gw::Step> list = steps(QStringLiteral("[]"));
        QCOMPARE(list.first().tool, gw::Step::Tool::Zypper);
        QCOMPARE(list.first().argv, QStringList({QStringLiteral("zypper"), QStringLiteral("-n"),
                                                 QStringLiteral("install"), QStringLiteral("wsdd")}));
        services("", "firewalld");
        list = steps(QStringLiteral("[]"));
        QCOMPARE(list.first().argv, QStringList({QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"),
                                                 QStringLiteral("samba"), QStringLiteral("wsdd")}));
    }

    // The steps every other caller reaches use the list applied now.
    void applyStepsUsesTheListInPlace()
    {
        const QString given = value({QStringLiteral("/mnt/kept")});
        fixture(kShares, written(given));
        bool wrote = false;
        for (const gw::Step &step : m_item.applySteps({}, context()))
            wrote = wrote || step.input.contains("path = /mnt/kept\n");
        QVERIFY(wrote);
    }
};

QTEST_GUILESS_MAIN(TstNetworkSharesItem)
#include "tst_networksharesitem.moc"
