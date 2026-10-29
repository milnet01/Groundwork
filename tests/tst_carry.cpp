// Carry (GRND-0078): gathering a setup and the saved file's format. Fake
// zypper and flatpak come first on PATH; tar is the real one, so the round
// trip is the file a user would get (spec § 7). GRND-0079 adds the items a
// saved file becomes, restored with the same real tar.
#include "carry/carry.h"
#include "carry/carrieditems.h"
#include "items/catalogue.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <unistd.h>

namespace {

class FakeItem : public gw::Item
{
public:
    FakeItem(QString id, bool preparation = false) : m_id(std::move(id)), m_prep(preparation) {}
    QString id() const override { return m_id; }
    gw::Level level() const override { return gw::Level::Essentials; }
    QString title() const override { return m_id; }
    QString applySentence() const override { return m_id; }
    bool isPreparation() const override { return m_prep; }
    gw::CheckResult check(const gw::CheckContext &) const override { return {}; }
    QList<gw::Step> applySteps(const gw::SystemIdentity &, const gw::CheckContext &) const override { return {}; }

private:
    QString m_id;
    bool m_prep;
};

// zypper search's table, with the lines it prints before it (measured
// 2026-10-19).
const QByteArray kZypperOut = "Loading repository data...\n"
                              "Reading installed packages...\n"
                              "\n"
                              "S  | Name          | Summary            | Type\n"
                              "---+---------------+--------------------+--------\n"
                              "i  | aaa_base      | openSUSE Base      | package\n"
                              "i+ | act           | Run GitHub actions | package\n"
                              "v  | act-doc       | Docs               | package\n"
                              "i+ | android-tools | Android tools      | package\n";

const QByteArray kOssRepo = "[repo-oss]\nname=Main Repository\nenabled=1\nautorefresh=1\n"
                            "baseurl=http://download.opensuse.org/tumbleweed/repo/oss/\ntype=rpm-md\n";
const QByteArray kHomeRepo = "[home_milnet_kde-fixes]\nname=KDE fixes\nenabled=1\nautorefresh=0\n"
                             "baseurl=https://download.opensuse.org/repositories/home:/milnet:/kde-fixes/"
                             "openSUSE_Tumbleweed/\ntype=rpm-md\npriority=80\ngpgcheck=1\n";
const QByteArray kReleaseverRepo = "[games]\nenabled=0\n"
                                   "baseurl=https://download.opensuse.org/repositories/games/$releasever/\n";

} // namespace

class TstCarry : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_bin, m_root, m_home, m_state;
    QByteArray m_path;

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
    QString home(const QString &relative) const { return m_home + QLatin1Char('/') + relative; }

    static int tar(const QStringList &args, const QByteArray &input = {}, QByteArray *out = nullptr)
    {
        QProcess p;
        p.start(QStringLiteral("tar"), args);
        p.waitForStarted();
        if (!input.isEmpty())
            p.write(input);
        p.closeWriteChannel();
        p.waitForFinished(30000);
        if (out)
            *out = p.readAllStandardOutput();
        return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    }
    QStringList members(const QString &file) const
    {
        QByteArray out;
        tar(gw::listArguments(file), {}, &out);
        return QString::fromUtf8(out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

    // A home with a Dolphin setting and an SSH key, and the manifest that
    // keeps both.
    gw::Manifest homeAndManifest()
    {
        writeFile(home(QStringLiteral(".config/dolphinrc")), "[General]\nShowFullPath=true\n");
        writeFile(home(QStringLiteral(".ssh/id_ed25519")), "not a real key\n");
        gw::Manifest m;
        m.created = QStringLiteral("2026-10-19T10:00:00Z");
        m.groundwork = QStringLiteral("0.5.0");
        m.systemId = QStringLiteral("opensuse-tumbleweed");
        m.systemVersion = QStringLiteral("20261014");
        m.items = {QStringLiteral("media-codecs")};
        m.packages = {QStringLiteral("act"), QStringLiteral("libstdc++6")};
        m.flatpaks = {{QStringLiteral("com.spotify.Client"), QStringLiteral("flathub"), QStringLiteral("system")}};
        gw::ZypperSource source;
        source.alias = QStringLiteral("home_milnet_kde-fixes");
        source.name = QStringLiteral("KDE fixes");
        source.baseurl = QStringLiteral("https://download.opensuse.org/repositories/home:/milnet:/kde-fixes/$releasever/");
        source.priority = 80;
        m.zypperSources = {source};
        m.flatpakRemotes = {{QStringLiteral("kdeapps"), QStringLiteral("https://distribute.kde.org/kdeapps.flatpakrepo"),
                             QStringLiteral("user")}};
        m.settings = {{QStringLiteral("Dolphin"), {QStringLiteral(".config/dolphinrc")}, false, 36},
                      {QStringLiteral("SSH keys"), {QStringLiteral(".ssh")}, true, 15}};
        return m;
    }

    // The Worker's steps 2 and 3 (spec 4.5), with real tar.
    QString save(const gw::Manifest &m)
    {
        writeFile(m_state + QLatin1Char('/') + gw::kManifestName, QJsonDocument(gw::manifestJson(m)).toJson());
        const QString file = m_dir->filePath(QStringLiteral("saved.tar"));
        const int code = tar(gw::createArguments(file, m_state, m_home), gw::createInput(m));
        return code == 0 ? file : QString();
    }

    std::optional<gw::Manifest> read(const QString &file, QString *why, QStringList *skipped = nullptr)
    {
        QStringList ignored;
        return gw::readSetupFile(gw::CheckContext(gw::FileReader()), file, why, skipped ? skipped : &ignored);
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_bin = m_dir->filePath(QStringLiteral("bin"));
        m_root = m_dir->filePath(QStringLiteral("root"));
        m_home = m_dir->filePath(QStringLiteral("home"));
        m_state = m_dir->filePath(QStringLiteral("state"));
        QVERIFY(QDir().mkpath(m_home) && QDir().mkpath(m_state) && QDir().mkpath(m_bin));
        m_path = qgetenv("PATH");
        qputenv("PATH", (m_bin + QStringLiteral(":/usr/bin:/bin")).toUtf8());
    }
    void cleanup() { qputenv("PATH", m_path); }

    // INV-10's patterns, with the spec's tables.
    void namesFollowTheirPatterns()
    {
        QVERIFY(gw::isPackageName(QStringLiteral("android-tools")));
        QVERIFY(gw::isPackageName(QStringLiteral("libstdc++6")));
        QVERIFY(!gw::isPackageName(QStringLiteral("-oRoot")));
        QVERIFY(!gw::isPackageName(QStringLiteral("a b")));
        QVERIFY(gw::isFlatpakAppId(QStringLiteral("com.spotify.Client")));
        QVERIFY(gw::isFlatpakAppId(QStringLiteral("org.kde.kate")));
        QVERIFY(!gw::isFlatpakAppId(QStringLiteral("spotify")));
        QVERIFY(!gw::isFlatpakAppId(QStringLiteral("-com.a.b")));
        QVERIFY(gw::isAlias(QStringLiteral("home_milnet_kde-fixes")));
        QVERIFY(gw::isAlias(QStringLiteral("download.opensuse.org-oss")));
        QVERIFY(!gw::isAlias(QStringLiteral("--gpg-auto-import-keys")));
        QVERIFY(gw::isSourceUrl(QStringLiteral("https://dl.example.org/repo/$releasever/")));
        QVERIFY(gw::isSourceUrl(QStringLiteral("ftp://ftp.example.org/")));
        QVERIFY(!gw::isSourceUrl(QStringLiteral("file:///mnt/cache")));
        QVERIFY(!gw::isSourceUrl(QStringLiteral("https://a b/")));
    }

    // INV-3's rule for a settings path.
    void aSettingsPathStaysInTheHomeFolder()
    {
        QVERIFY(gw::isSettingsPath(QStringLiteral(".config/dolphinrc")));
        QVERIFY(gw::isSettingsPath(QStringLiteral(".local/share/fonts")));
        QVERIFY(gw::isSettingsPath(QStringLiteral(".ssh")));
        for (const char *bad : {"../x", "/etc/x", ".config/../../x", "x", "", ".cache", ".local", ".var", ".local/",
                                ".", "./.ssh"})
            QVERIFY2(!gw::isSettingsPath(QString::fromLatin1(bad)), bad);
    }

    // INV-8: a preparation is never recorded, nor an item not done.
    void onlyDoneItemsThatAreNotPreparationsAreGathered()
    {
        const FakeItem update(QStringLiteral("system-update"), true);
        const FakeItem codecs(QStringLiteral("media-codecs"));
        const FakeItem fonts(QStringLiteral("fonts"));
        const gw::Catalogue catalogue({&update, &codecs, &fonts});
        gw::CheckResults results;
        results.insert(QStringLiteral("system-update"), {gw::CheckState::Done, {}});
        results.insert(QStringLiteral("media-codecs"), {gw::CheckState::Done, {}});
        results.insert(QStringLiteral("fonts"), {gw::CheckState::NotDone, {}});
        QCOMPARE(gw::gatherItems(catalogue, results), QStringList{QStringLiteral("media-codecs")});
    }

    // INV-7: only i+ rows, by name, trimmed.
    void onlyAskedForPackagesAreGathered()
    {
        QCOMPARE(gw::askedForPackages(kZypperOut), (QStringList{QStringLiteral("act"), QStringLiteral("android-tools")}));
        writeFile(m_bin + QStringLiteral("/zypper"), "#!/bin/sh\ncat \"$FAKE_ZYPPER_OUT\"\n", true);
        writeFile(m_bin + QStringLiteral("/flatpak"), "#!/bin/sh\nprintf 'com.spotify.Client\\tflathub\\tsystem\\n'\n", true);
        writeFile(m_dir->filePath(QStringLiteral("zypper-out")), kZypperOut);
        qputenv("FAKE_ZYPPER_OUT", m_dir->filePath(QStringLiteral("zypper-out")).toUtf8());
        gw::Gathered g;
        gw::gatherPrograms(gw::CheckContext(gw::FileReader(m_root)), &g);
        QCOMPARE(g.packages, (QStringList{QStringLiteral("act"), QStringLiteral("android-tools")}));
        QVERIFY(g.packagesProblem.isEmpty());
        QCOMPARE(g.flatpaks.size(), 1);
        QCOMPARE(g.flatpaks.first(),
                 (gw::FlatpakApp{QStringLiteral("com.spotify.Client"), QStringLiteral("flathub"), QStringLiteral("system")}));
    }

    // Spec 6: a command that is missing makes its group "couldn't tell".
    void aMissingCommandMakesItsGroupCouldNotTell()
    {
        qputenv("PATH", m_bin.toUtf8()); // no zypper, no flatpak
        gw::Gathered g;
        gw::gatherPrograms(gw::CheckContext(gw::FileReader(m_root)), &g);
        QVERIFY(g.packages.isEmpty());
        QVERIFY(!g.packagesProblem.isEmpty());
        QVERIFY(!g.flatpaksProblem.isEmpty());
    }

    // INV-6: a distribution tree is left out; any other source is kept,
    // with its baseurl as its file writes it. A file that cannot be parsed
    // is named. Flathub is left out of the remotes.
    void sourcesTheUserAddedAreGathered()
    {
        const QString repos = m_root + QStringLiteral("/etc/zypp/repos.d/");
        writeFile(repos + QStringLiteral("repo-oss.repo"), kOssRepo);
        writeFile(repos + QStringLiteral("home_milnet_kde-fixes.repo"), kHomeRepo);
        writeFile(repos + QStringLiteral("games.repo"), kReleaseverRepo);
        writeFile(repos + QStringLiteral("broken.repo"), "this is not a repo file\n");
        writeFile(m_bin + QStringLiteral("/flatpak"),
                  "#!/bin/sh\nprintf 'flathub\\thttps://dl.flathub.org/repo/\\tsystem\\n"
                  "kdeapps\\thttps://distribute.kde.org/kdeapps/\\tuser,no-enumerate\\n"
                  "local-origin\\tfile:///mnt/cache\\tuser,no-gpg-verify\\n'\n",
                  true);
        gw::Gathered g;
        gw::gatherSources(gw::CheckContext(gw::FileReader(m_root)), &g);
        QCOMPARE(g.zypperSources.size(), 2);
        const gw::ZypperSource &games = g.zypperSources[0]; // in file name order
        QCOMPARE(games.alias, QStringLiteral("games"));
        QCOMPARE(games.name, QStringLiteral("games")); // no name: the alias
        QCOMPARE(games.baseurl, QStringLiteral("https://download.opensuse.org/repositories/games/$releasever/"));
        QVERIFY(!games.enabled);
        const gw::ZypperSource &home = g.zypperSources[1];
        QCOMPARE(home.alias, QStringLiteral("home_milnet_kde-fixes"));
        QCOMPARE(home.name, QStringLiteral("KDE fixes"));
        QCOMPARE(home.priority, 80);
        QVERIFY(home.enabled && !home.autorefresh && home.gpgcheck);
        QCOMPARE(g.unreadableRepos, QStringList{QStringLiteral("broken.repo")});
        QCOMPARE(g.flatpakRemotes.size(), 1);
        QCOMPARE(g.flatpakRemotes.first(), (gw::FlatpakRemote{QStringLiteral("kdeapps"),
                                                                QStringLiteral("https://distribute.kde.org/kdeapps/"),
                                                                QStringLiteral("user")}));
        QVERIFY(gw::isDistributionSource(QStringLiteral("https://download.opensuse.org/update/leap/16.0/oss/")));
        QVERIFY(gw::isDistributionSource(QStringLiteral("http://codecs.opensuse.org/openh264/openSUSE_Tumbleweed")));
        QVERIFY(!gw::isDistributionSource(QStringLiteral("https://download.opensuse.org/repositories/games/")));
    }

    // Spec 4.3, 4.7: a source the reader would refuse for its gpgkey or
    // priority is not gathered, but named with why; a gpgkey is https only
    // (user decision 2026-10-29).
    void aSourceTheReaderWouldRefuseIsNotGathered()
    {
        const QString repos = m_root + QStringLiteral("/etc/zypp/repos.d/");
        const QByteArray url = "baseurl=https://dl.example.org/repo/\n";
        writeFile(repos + QStringLiteral("a.repo"), "[a-http-key]\n" + url + "gpgkey=http://dl.example.org/key\n");
        writeFile(repos + QStringLiteral("b.repo"), "[b-file-key]\n" + url + "gpgkey=file:///etc/key\n");
        writeFile(repos + QStringLiteral("c.repo"),
                  "[c-two-keys]\n" + url + "gpgkey=https://dl.example.org/k1 https://dl.example.org/k2\n");
        writeFile(repos + QStringLiteral("d.repo"), "[d-priority-0]\n" + url + "priority=0\n");
        writeFile(repos + QStringLiteral("e.repo"), "[e-priority-word]\n" + url + "priority=high\n");
        writeFile(repos + QStringLiteral("f.repo"), "[f-good]\n" + url + "gpgkey=https://dl.example.org/key\npriority=200\n");
        gw::Gathered g;
        gw::gatherSources(gw::CheckContext(gw::FileReader(m_root)), &g);
        QCOMPARE(g.zypperSources.size(), 1);
        QCOMPARE(g.zypperSources.first().alias, QStringLiteral("f-good"));
        QCOMPARE(g.uncarriedSources.size(), 5);
        const QString why = g.uncarriedSources.join(QLatin1Char('\n'));
        for (const char *alias : {"a-http-key", "b-file-key", "c-two-keys", "d-priority-0", "e-priority-word"})
            QVERIFY2(why.contains(QLatin1String(alias)), alias);
        QVERIFY(gw::isKeyUrl(QStringLiteral("https://dl.example.org/key")));
        QVERIFY(!gw::isKeyUrl(QStringLiteral("http://dl.example.org/key")));
        QVERIFY(!gw::isKeyUrl(QStringLiteral("ftp://dl.example.org/key")));

        // The reader refuses the same: a saved http key is skipped.
        QJsonObject json = gw::manifestJson(homeAndManifest());
        QJsonArray sources = json.value("sources").toArray();
        QJsonObject plain = sources.first().toObject();
        plain.insert("alias", "plain-key");
        plain.insert("gpgkey", "http://dl.example.org/key");
        sources.append(plain);
        json.insert("sources", sources);
        QString refused;
        QStringList skipped;
        const std::optional<gw::Manifest> m = gw::readManifest(json, &refused, &skipped);
        QVERIFY2(m, qPrintable(refused));
        QCOMPARE(m->zypperSources.size(), 1);
        QVERIFY(skipped.join(QLatin1Char('\n')).contains(QLatin1String("plain-key")));
    }

    // Spec 4.4: an app's entries are one row; an entry no group claims is
    // its own; data in .local/share and .cache is not offered.
    void settingsAreOfferedAppByApp()
    {
        const QList<gw::SettingsRow> rows = gw::groupSettings(
            {QStringLiteral(".config/kdeglobals"), QStringLiteral(".config/kwinrc"),
             QStringLiteral(".config/plasma-org.kde.plasma.desktop-appletsrc"), QStringLiteral(".config/dolphinrc"),
             QStringLiteral(".config/someapp")});
        QStringList names;
        for (const gw::SettingsRow &row : rows)
            names << row.name;
        QVERIFY2(names.contains(QStringLiteral("KDE Plasma desktop")), qPrintable(names.join(", ")));
        QCOMPARE(names.size(), 3);
        for (const gw::SettingsRow &row : rows) {
            if (row.name == QLatin1String("KDE Plasma desktop"))
                QCOMPARE(row.paths.size(), 3);
            if (row.paths.contains(QStringLiteral(".config/someapp")))
                QCOMPARE(row.name, QStringLiteral(".config/someapp"));
        }

        writeFile(home(QStringLiteral(".config/dolphinrc")), "x");
        writeFile(home(QStringLiteral(".cache/thumbs/a")), "x");
        writeFile(home(QStringLiteral(".local/share/fonts/a.ttf")), "x");
        writeFile(home(QStringLiteral(".local/share/Steam/game")), "x");
        writeFile(home(QStringLiteral(".var/app/org.kde.kate/config/katerc")), "x");
        writeFile(home(QStringLiteral(".var/app/org.kde.kate/data/big")), "x");
        writeFile(home(QStringLiteral(".bashrc")), "x");
        QStringList offered;
        for (const gw::SettingsRow &row : gw::gatherSettings(gw::CheckContext(gw::FileReader()), m_home))
            offered << row.paths;
        offered.sort();
        QCOMPARE(offered, (QStringList{QStringLiteral(".bashrc"), QStringLiteral(".config/dolphinrc"),
                                       QStringLiteral(".local/share/fonts"),
                                       QStringLiteral(".var/app/org.kde.kate/config")}));
    }

    // INV-2: a secret row starts switched off, and is recorded as secret
    // when kept. A row bigger than 100 MiB starts off too.
    void secretsStartSwitchedOff()
    {
        writeFile(home(QStringLiteral(".ssh/id_ed25519")), "not a real key\n");
        writeFile(home(QStringLiteral(".config/BraveSoftware/Default/Login Data")), "x");
        writeFile(home(QStringLiteral(".config/dolphinrc")), "[General]\n");
        const QList<gw::SettingsRow> rows = gw::gatherSettings(gw::CheckContext(gw::FileReader()), m_home);
        QCOMPARE(rows.size(), 3);
        for (const gw::SettingsRow &row : rows) {
            const bool secret = !row.paths.contains(QStringLiteral(".config/dolphinrc"));
            QCOMPARE(row.secret, secret);
            QCOMPARE(gw::startsOn(row), !secret);
            QVERIFY2(row.bytes > 0, qPrintable(row.name)); // du read it
        }
        // Native Discord keeps its login token in .config/discord; the
        // Flatpak browsers keep their profiles under config.
        for (const char *path : {".config/discord", ".var/app/com.vivaldi.Vivaldi/config", ".var/app/com.opera.Opera/config"}) {
            const QList<gw::SettingsRow> one = gw::groupSettings({QString::fromLatin1(path)});
            QVERIFY2(one.size() == 1 && one.first().secret, path);
        }
        QVERIFY(!gw::startsOn({QStringLiteral("Steam"), {QStringLiteral(".steam")}, false, gw::kLargeSettings + 1}));
        QVERIFY(gw::startsOn({QStringLiteral("Small"), {QStringLiteral(".small")}, false, gw::kLargeSettings}));

        gw::Manifest m;
        m.settings = {rows.first()};
        for (const gw::SettingsRow &row : rows)
            if (row.secret)
                m.settings = {row};
        const QJsonObject kept = gw::manifestJson(m).value("settings").toArray().first().toObject();
        QCOMPARE(kept.value("secret"), QJsonValue(true));
    }

    // Spec 4.4: a size du did not give is unknown, not 0. The row starts
    // off, and Save waits while it is on.
    void aSizeDuDidNotGiveIsUnknown()
    {
        writeFile(home(QStringLiteral(".config/dolphinrc")), "[General]\n");
        writeFile(home(QStringLiteral(".bashrc")), "x");
        // du that printed sizes, then failed: none of them is trusted.
        writeFile(m_bin + QStringLiteral("/du"), "#!/bin/sh\nshift 3\nfor p; do printf '1\\t%s\\n' \"$p\"; done\nexit 2\n",
                  true);
        QList<gw::SettingsRow> rows = gw::gatherSettings(gw::CheckContext(gw::FileReader()), m_home);
        QCOMPARE(rows.size(), 2);
        for (const gw::SettingsRow &row : rows) {
            QVERIFY2(row.bytes < 0, qPrintable(row.name));
            QVERIFY(!gw::startsOn(row));
        }
        // du ran but named one path only: the other is unknown.
        writeFile(m_bin + QStringLiteral("/du"),
                  "#!/bin/sh\nfor p; do case \"$p\" in *bashrc) printf '1\\t%s\\n' \"$p\";; esac; done\n", true);
        rows = gw::gatherSettings(gw::CheckContext(gw::FileReader()), m_home);
        for (const gw::SettingsRow &row : rows)
            QCOMPARE(row.bytes < 0, row.paths.contains(QStringLiteral(".config/dolphinrc")));
        // Sizes, but no count of entries: unknown too.
        writeFile(m_bin + QStringLiteral("/du"),
                  "#!/bin/sh\n[ \"$2\" = -b ] || exit 2\nshift 3\nfor p; do printf '1\\t%s\\n' \"$p\"; done\n", true);
        for (const gw::SettingsRow &row : gw::gatherSettings(gw::CheckContext(gw::FileReader()), m_home))
            QVERIFY2(row.bytes < 0, qPrintable(row.name));
        QVERIFY(!gw::saveProblem(-1, qint64(100) << 30, QStringLiteral("ext4")).isEmpty());
    }

    // INV-9, spec 4.4: a row's size is what tar writes for it, headers
    // and padding too, not the sum of its files.
    void aRowIsSizedAsTheArchiveHoldsIt()
    {
        for (int i = 0; i < 300; ++i)
            writeFile(home(QStringLiteral(".config/many/f%1").arg(i)), "x");
        gw::Manifest m;
        m.settings = gw::gatherSettings(gw::CheckContext(gw::FileReader()), m_home);
        QCOMPARE(m.settings.size(), 1);
        const QString file = save(m);
        QVERIFY(!file.isEmpty());
        const qint64 members = QFileInfo(file).size() - 16 * 1024; // less the manifest and the end
        QVERIFY2(m.settings.first().bytes >= members,
                 qPrintable(QStringLiteral("%1 < %2").arg(m.settings.first().bytes).arg(members)));
    }

    // INV-9: Save waits for room, and for a file vfat can hold.
    void saveNeedsRoomAndAFileTheFolderCanHold()
    {
        constexpr qint64 GiB = qint64(1) << 30;
        QVERIFY(!gw::saveProblem(5 * GiB, 100 * GiB, QStringLiteral("vfat")).isEmpty());
        QVERIFY(gw::saveProblem(5 * GiB, 100 * GiB, QStringLiteral("ext4")).isEmpty());
        QVERIFY(!gw::saveProblem(GiB, GiB / 2, QStringLiteral("ext4")).isEmpty());
        QVERIFY(gw::saveProblem(gw::kVfatLimit - gw::kSetupFileExtra, 100 * GiB, QStringLiteral("vfat")).isEmpty());
        QVERIFY(!gw::saveProblem(gw::kVfatLimit - gw::kSetupFileExtra + 1, 100 * GiB, QStringLiteral("vfat")).isEmpty());
        QVERIFY(!gw::saveProblem(GiB, GiB + gw::kSetupFileExtra - 1, QStringLiteral("ext4")).isEmpty());
        QVERIFY(!gw::saveProblem(GiB, -1, QStringLiteral("ext4")).isEmpty()); // free space unknown
    }

    void theFileIsNamedForTheComputerAndTheDay()
    {
        QCOMPARE(gw::setupFileName(QStringLiteral("lounge-pc"), QDate(2026, 10, 19)),
                 QStringLiteral("groundwork-setup-lounge-pc-2026-10-19.tar"));
    }

    // INV-1: a saved file reads back to an equal manifest.
    void aSavedFileReadsBackTheSame()
    {
        const gw::Manifest m = homeAndManifest();
        const QString file = save(m);
        QVERIFY(!file.isEmpty());
        const QStringList listed = members(file);
        QCOMPARE(listed.first(), gw::kManifestName); // written first
        QVERIFY(gw::membersProblem(listed, m).isEmpty());
        QString why;
        const std::optional<gw::Manifest> back = read(file, &why);
        QVERIFY2(back, qPrintable(why));
        QCOMPARE(*back, m);
    }

    // INV-1: a later format-1 writer may add keys; a newer format is
    // refused.
    void unknownKeysAreIgnoredAndANewerFormatIsRefused()
    {
        QJsonObject json = gw::manifestJson(homeAndManifest());
        json.insert("added-later", QJsonObject{{"anything", 1}});
        QString why;
        QStringList skipped;
        QVERIFY(gw::readManifest(json, &why, &skipped));
        QVERIFY(skipped.isEmpty());
        json.insert("format", 2);
        QVERIFY(!gw::readManifest(json, &why, &skipped));
        QVERIFY2(why.contains(QLatin1String("newer Groundwork")), qPrintable(why));
        QVERIFY(!gw::readManifest(QByteArray("not json"), &why, &skipped));
    }

    // INV-3: a member outside the home folder, or under no listed setting,
    // refuses the whole file.
    void anArchiveWithAMemberOutsideItsSettingsIsRefused()
    {
        const gw::Manifest m = homeAndManifest();
        writeFile(m_state + QLatin1Char('/') + gw::kManifestName, QJsonDocument(gw::manifestJson(m)).toJson());
        writeFile(m_dir->filePath(QStringLiteral("x")), "outside");
        writeFile(home(QStringLiteral(".unlisted")), "x");
        const QString base = m_dir->filePath(QStringLiteral("bad"));
        const QList<QStringList> extra{
            {QStringLiteral("-C"), m_home, QStringLiteral("../x")},
            {m_dir->filePath(QStringLiteral("x"))}, // an absolute member
            {QStringLiteral("-C"), m_home, QStringLiteral(".unlisted")},
        };
        for (qsizetype i = 0; i < extra.size(); ++i) {
            const QString file = base + QString::number(i) + QStringLiteral(".tar");
            QCOMPARE(tar(QStringList{QStringLiteral("--create"), QStringLiteral("--absolute-names"),
                                     QStringLiteral("--file"), file, QStringLiteral("-C"), m_state, gw::kManifestName,
                                     QStringLiteral("-C"), m_home, QStringLiteral(".config/dolphinrc"),
                                     QStringLiteral(".ssh")}
                         + extra[i]),
                     0);
            QString why;
            QVERIFY2(!read(file, &why), qPrintable(file));
            QVERIFY(!why.isEmpty());
        }
        // The manifest twice is refused too.
        const QString twice = base + QStringLiteral("-twice.tar");
        QCOMPARE(tar({QStringLiteral("--create"), QStringLiteral("--file"), twice, QStringLiteral("-C"), m_state,
                      gw::kManifestName, gw::kManifestName}),
                 0);
        QString why;
        QVERIFY(!read(twice, &why));
    }

    // INV-5's comparison: a listed setting missing from the archive fails.
    void aListedSettingMissingFromTheArchiveIsAMismatch()
    {
        const gw::Manifest m = homeAndManifest();
        QVERIFY(!gw::membersProblem({gw::kManifestName, QStringLiteral(".config/dolphinrc")}, m).isEmpty());
        QVERIFY(gw::membersProblem({gw::kManifestName, QStringLiteral(".config/dolphinrc"), QStringLiteral(".ssh/"),
                                    QStringLiteral(".ssh/id_ed25519")},
                                   m)
                    .isEmpty());
    }

    // GRND-0085, spec 4.5 step 2: a path tar cannot read at all is left out
    // of the manifest, and so is a row left with no path. A folder that can
    // be read but not entered keeps its path: tar writes its member.
    void aPathTarCannotReadIsLeftOutOfTheManifest()
    {
        if (getuid() == 0)
            QSKIP("root reads every file");
        gw::Manifest m = homeAndManifest();
        writeFile(home(QStringLiteral(".viminfo")), "x");
        writeFile(home(QStringLiteral(".config/locked/a")), "x");
        m.settings << gw::SettingsRow{QStringLiteral("Vim"), {QStringLiteral(".viminfo")}, false, 1}
                   << gw::SettingsRow{QStringLiteral("Locked"), {QStringLiteral(".config/locked")}, false, 1};
        m.settings[0].paths << QStringLiteral(".viminfo");
        QVERIFY(QFile::setPermissions(home(QStringLiteral(".viminfo")), {}));
        QVERIFY(QFile::setPermissions(home(QStringLiteral(".config/locked")), QFile::ReadOwner));
        const gw::Manifest kept = gw::withoutUnreadablePaths(m, m_home);
        QFile::setPermissions(home(QStringLiteral(".viminfo")), QFile::ReadOwner | QFile::WriteOwner);
        QFile::setPermissions(home(QStringLiteral(".config/locked")),
                              QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QCOMPARE(kept.settings.size(), 3);
        QCOMPARE(kept.settings[0].paths, QStringList{QStringLiteral(".config/dolphinrc")});
        QCOMPARE(kept.settings[1].name, QStringLiteral("SSH keys"));
        QCOMPARE(kept.settings[2].paths, QStringList{QStringLiteral(".config/locked")});
    }

    // INV-4: § 4.8's steps remove the secrets from the file alone and keep
    // the rest.
    void secretsCanBeRemovedFromTheFileAlone()
    {
        const gw::Manifest m = homeAndManifest();
        const QString file = save(m);
        QVERIFY(!file.isEmpty());
        const QStringList secret = gw::secretMembers(members(file), m);
        QCOMPARE(secret, QStringList{QStringLiteral(".ssh")}); // its key goes with it
        QCOMPARE(tar(QStringList{QStringLiteral("--delete"), QStringLiteral("--file"), file} + secret), 0);
        QCOMPARE(tar({QStringLiteral("--delete"), QStringLiteral("--file"), file, gw::kManifestName}), 0);
        const gw::Manifest kept = gw::withoutSecrets(m);
        writeFile(m_state + QLatin1Char('/') + gw::kManifestName, QJsonDocument(gw::manifestJson(kept)).toJson());
        QCOMPARE(tar({QStringLiteral("--append"), QStringLiteral("--file"), file, QStringLiteral("-C"), m_state,
                      gw::kManifestName}),
                 0);
        for (const QString &member : members(file))
            QVERIFY2(!member.startsWith(QLatin1String(".ssh")), qPrintable(member));
        QString why;
        const std::optional<gw::Manifest> back = read(file, &why);
        QVERIFY2(back, qPrintable(why));
        QCOMPARE(*back, kept);
        QCOMPARE(back->settings.size(), 1);
        QCOMPARE(back->packages, m.packages);
    }

    // INV-10: an entry whose field fails its pattern is skipped, saying
    // why; the rest is read.
    void anEntryFailingItsPatternIsSkipped()
    {
        QJsonObject json = gw::manifestJson(homeAndManifest());
        json.insert("packages", QJsonArray{"act", "-oRoot"});
        QJsonArray sources = json.value("sources").toArray();
        QJsonObject bad = sources.first().toObject();
        bad.insert("alias", "--gpg-auto-import-keys");
        sources.append(bad);
        json.insert("sources", sources);
        QString why;
        QStringList skipped;
        const std::optional<gw::Manifest> m = gw::readManifest(json, &why, &skipped);
        QVERIFY2(m, qPrintable(why));
        QCOMPARE(m->packages, QStringList{QStringLiteral("act")});
        QCOMPARE(m->zypperSources.size(), 1);
        QCOMPARE(skipped.size(), 2);
        QVERIFY(skipped.join(QLatin1Char('\n')).contains(QLatin1String("-oRoot")));
        QVERIFY(skipped.join(QLatin1Char('\n')).contains(QLatin1String("--gpg-auto-import-keys")));
    }

    // GRND-0086: what the installer chooses for this computer, a kernel,
    // boot loader or security framework, is neither gathered nor read
    // back, even from a file saved before; a kernel's companions are.
    void whatTheInstallerChoosesIsLeftOut()
    {
        const QStringList chosen{QStringLiteral("kernel-default"), QStringLiteral("kernel-default-base"),
                                 QStringLiteral("kernel-longterm"),
                                 QStringLiteral("grub2-x86_64-efi-bls"),
                                 QStringLiteral("grub2"),
                                 QStringLiteral("shim"),
                                 QStringLiteral("sdbootutil"),
                                 QStringLiteral("systemd-boot"),
                                 QStringLiteral("patterns-base-selinux"),
                                 QStringLiteral("patterns-base-apparmor")};
        const QStringList kept{QStringLiteral("act"), QStringLiteral("kernel-devel"),
                               QStringLiteral("kernel-default-devel"), QStringLiteral("kernel-firmware-amdgpu"),
                               QStringLiteral("mokutil")};
        QByteArray table = "S  | Name | Summary | Type\n---+------+---------+--------\n";
        for (const QString &name : chosen + kept)
            table += "i+ | " + name.toUtf8() + " | x | package\n";
        QCOMPARE(gw::askedForPackages(table), kept);

        QJsonObject json = gw::manifestJson(homeAndManifest());
        json.insert("packages", QJsonArray::fromStringList(chosen + kept));
        QString why;
        QStringList skipped;
        const std::optional<gw::Manifest> m = gw::readManifest(json, &why, &skipped);
        QVERIFY2(m, qPrintable(why));
        QCOMPARE(m->packages, kept);
        QVERIFY(skipped.isEmpty()); // left out by design, not unreadable
    }

    // GRND-0079, INV-3: a carried id holds a colon or starts "carried-",
    // and none is a Groundwork id.
    void aCarriedIdIsNeverAGroundworkId()
    {
        const gw::CarriedItems carried = gw::carriedItems(homeAndManifest(), QStringLiteral("f.tar"), gw::catalogue());
        QCOMPARE(carried.owned.size(), size_t(4)); // a source, programs, Flatpak apps, settings
        for (const auto &item : carried.owned) {
            QVERIFY2(item->id().contains(QLatin1Char(':')) || item->id().startsWith(QLatin1String("carried-")),
                     qPrintable(item->id()));
            QVERIFY(gw::isCarriedId(item->id()));
            QVERIFY(!gw::catalogue().find(item->id()));
        }
        for (const gw::Item *item : gw::catalogue().items())
            QVERIFY2(!gw::isCarriedId(item->id()), qPrintable(item->id()));
        QVERIFY(gw::withCarried(gw::catalogue(), carried).orderProblems().isEmpty());
    }

    // GRND-0079, INV-7: a source already present under another alias reads
    // done by its baseurl; a new one is written whole, $releasever and all.
    void aSourceIsWrittenWholeAndOnce()
    {
        const QByteArray packman = "[packman-gwdg]\nname=Packman\nenabled=1\n"
                                   "baseurl=https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/\n";
        writeFile(m_root + QStringLiteral("/etc/zypp/repos.d/packman-gwdg.repo"), packman);
        gw::Manifest m;
        gw::ZypperSource kept;
        kept.alias = QStringLiteral("packman");
        kept.name = QStringLiteral("Packman");
        kept.baseurl = QStringLiteral("https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/");
        m.zypperSources << kept << homeAndManifest().zypperSources;
        m.zypperSources[1].gpgkey = QStringLiteral("https://download.opensuse.org/repositories/home:/milnet/repodata/repomd.xml.key");
        const gw::CarriedItems carried = gw::carriedItems(m, QStringLiteral("f.tar"), gw::catalogue());
        QCOMPARE(carried.owned.size(), size_t(2));
        const gw::CheckContext context{gw::FileReader(m_root)};
        QCOMPARE(carried.owned[0]->check(context).state, gw::CheckState::Done);
        QCOMPARE(carried.owned[1]->check(context).state, gw::CheckState::NotDone);

        const QList<gw::Step> steps = carried.owned[1]->applySteps({}, context);
        QCOMPARE(steps.size(), 4);
        const QString staged = gw::kStagedReposDir + QStringLiteral("/home_milnet_kde-fixes.repo");
        QCOMPARE(steps[1].argv, (QStringList{QStringLiteral("dd"), QStringLiteral("of=") + staged, QStringLiteral("status=none")}));
        QCOMPARE(QString::fromUtf8(steps[1].input),
                 QStringLiteral("[home_milnet_kde-fixes]\nname=KDE fixes\n"
                                "baseurl=https://download.opensuse.org/repositories/home:/milnet:/kde-fixes/$releasever/\n"
                                "priority=80\nenabled=1\nautorefresh=0\ngpgcheck=1\n"
                                "gpgkey=https://download.opensuse.org/repositories/home:/milnet/repodata/repomd.xml.key\n"));
        // Refreshed where it was staged, then moved into place.
        QCOMPARE(steps[2].argv.mid(0, 4), (QStringList{QStringLiteral("zypper"), QStringLiteral("-n"),
                                                       QStringLiteral("-D"), gw::kStagedReposDir}));
        QCOMPARE(steps[2].argv.last(), QStringLiteral("home_milnet_kde-fixes"));
        QCOMPARE(steps[3].argv.last(), QStringLiteral("/etc/zypp/repos.d/home_milnet_kde-fixes.repo"));
        for (const gw::Step &step : steps)
            QVERIFY(step.needsRoot);
    }

    // GRND-0079 § 4.5: settings saved by another account, whose uid,
    // group and modes differ from what the restore left, are done once
    // their contents match. tar --diff reports owner and mode too.
    void settingsFromAnotherAccountAreDone()
    {
        const gw::Manifest m = homeAndManifest();
        writeFile(m_state + QLatin1Char('/') + gw::kManifestName, QJsonDocument(gw::manifestJson(m)).toJson());
        const QString file = m_dir->filePath(QStringLiteral("other.tar"));
        QCOMPARE(tar(QStringList{QStringLiteral("--owner=12345"), QStringLiteral("--group=12345"),
                                 QStringLiteral("--mode=a+rw")}
                         + gw::createArguments(file, m_state, m_home),
                     gw::createInput(m)),
                 0);
        const QByteArray homePath = qgetenv("HOME");
        qputenv("HOME", m_home.toUtf8());
        const gw::CarriedItems carried = gw::carriedItems(m, file, gw::catalogue());
        qputenv("HOME", homePath);
        const gw::Item &settings = *carried.owned.back();
        QCOMPARE(settings.id(), gw::kCarriedSettingsId);
        const gw::CheckContext context{gw::FileReader()};
        QCOMPARE(settings.check(context).state, gw::CheckState::Done);
        writeFile(home(QStringLiteral(".config/dolphinrc")), "[General]\nShowFullPath=false\n");
        QCOMPARE(settings.check(context).state, gw::CheckState::NotDone);
        QFile::remove(home(QStringLiteral(".ssh/id_ed25519")));
        writeFile(home(QStringLiteral(".config/dolphinrc")), "[General]\nShowFullPath=true\n");
        QCOMPARE(settings.check(context).state, gw::CheckState::NotDone);
    }

    // GRND-0079, INV-6: a file a setting replaces is kept, and a second
    // restore keeps the first copy.
    void aReplacedSettingIsKeptAsANumberedCopy()
    {
        const gw::Manifest m = homeAndManifest();
        const QString file = save(m);
        QVERIFY(!file.isEmpty());
        const QByteArray homePath = qgetenv("HOME");
        qputenv("HOME", m_home.toUtf8());
        const gw::CarriedItems carried = gw::carriedItems(m, file, gw::catalogue());
        qputenv("HOME", homePath);
        const gw::Item &settings = *carried.owned.back();
        QCOMPARE(settings.id(), gw::kCarriedSettingsId);
        const gw::CheckContext context{gw::FileReader()};
        QCOMPARE(settings.check(context).state, gw::CheckState::Done);

        writeFile(home(QStringLiteral(".config/dolphinrc")), "new install's own\n");
        QCOMPARE(settings.check(context).state, gw::CheckState::NotDone);
        const QList<gw::Step> steps = settings.applySteps({}, context);
        QCOMPARE(steps.size(), 1);
        QVERIFY(!steps[0].needsRoot);
        QCOMPARE(steps[0].argv.mid(0, 5), (QStringList{QStringLiteral("tar"), QStringLiteral("--extract"),
                                                       QStringLiteral("--file"), file, QStringLiteral("-C")}));
        QCOMPARE(steps[0].argv.value(5), m_home);
        QCOMPARE(tar(steps[0].argv.mid(1), steps[0].input), 0);
        QCOMPARE(settings.check(context).state, gw::CheckState::Done);
        writeFile(home(QStringLiteral(".config/dolphinrc")), "changed again\n");
        QCOMPARE(tar(steps[0].argv.mid(1), steps[0].input), 0);

        QFile first(home(QStringLiteral(".config/dolphinrc.~1~")));
        QVERIFY(first.open(QIODevice::ReadOnly));
        QCOMPARE(first.readAll(), QByteArray("new install's own\n"));
        QVERIFY(QFile::exists(home(QStringLiteral(".config/dolphinrc.~2~"))));
    }

    // GRND-0079, INV-5: an archive holding a link out of the home folder,
    // then a file under it, writes nothing outside.
    void aSettingCannotWriteThroughALinkOutOfTheHomeFolder()
    {
        const QString outside = m_dir->filePath(QStringLiteral("outside"));
        const QString first = m_dir->filePath(QStringLiteral("first"));
        const QString second = m_dir->filePath(QStringLiteral("second"));
        QVERIFY(QDir().mkpath(outside) && QDir().mkpath(first + QStringLiteral("/.config"))
                && QDir().mkpath(second + QStringLiteral("/.config/evil")));
        QVERIFY(QFile::link(outside, first + QStringLiteral("/.config/evil")));
        writeFile(second + QStringLiteral("/.config/evil/pwned"), "written outside\n");
        const QString file = m_dir->filePath(QStringLiteral("evil.tar"));
        QCOMPARE(tar({QStringLiteral("--create"), QStringLiteral("--file"), file, QStringLiteral("-C"), first,
                      QStringLiteral(".config/evil")}),
                 0);
        QCOMPARE(tar({QStringLiteral("--append"), QStringLiteral("--file"), file, QStringLiteral("-C"), second,
                      QStringLiteral(".config/evil/pwned")}),
                 0);

        gw::Manifest m;
        m.settings = {{QStringLiteral("Evil"), {QStringLiteral(".config/evil")}, false, 1}};
        const QByteArray homePath = qgetenv("HOME");
        qputenv("HOME", m_home.toUtf8());
        const gw::CarriedItems carried = gw::carriedItems(m, file, gw::catalogue());
        qputenv("HOME", homePath);
        const QList<gw::Step> steps = carried.owned.back()->applySteps({}, gw::CheckContext(gw::FileReader()));
        QVERIFY(!steps[0].needsRoot);
        QCOMPARE(tar(steps[0].argv.mid(1), steps[0].input), 2);
        QVERIFY(!QFile::exists(outside + QStringLiteral("/pwned")));
    }

    // GRND-0079, INV-2's rule: an entry the file lacks is named.
    void anEntryTheFileLacksIsNamed()
    {
        const gw::Manifest file = homeAndManifest();
        gw::Manifest chosen = file;
        chosen.settings.removeLast();
        QVERIFY(gw::notInFile(chosen, file).isEmpty());
        chosen.packages << QStringLiteral("evil");
        QVERIFY(gw::notInFile(chosen, file).contains(QLatin1String("evil")));
    }
};

QTEST_GUILESS_MAIN(TstCarry)
#include "tst_carry.moc"
