// GRND-0065: the GitHub part, and the built program refusing a bad choices
// file, with a fake gh recording every command. These prove what Groundwork
// asks of gh, not what GitHub does (spec 7).
#include "core/filereader.h"
#include "github/github.h"
#include "gui/githubpages.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>
#include <QWizard>

namespace {

// Signs in by printing a code and waiting for the file FAKE_GH_CONTINUE, then
// answers from what the test set. No account, repository or token is real.
const QByteArray kGh = R"(#!/bin/sh
echo "gh $*" >> "$FAKE_LOG"
case "$1 $2" in
  "auth status") [ -f "$FAKE_SIGNED_IN" ] && exit 0; exit 1;;
  "auth login")
    echo '! One-time code (ABCD-1234) copied to clipboard' >&2
    echo 'Open this URL to continue in your web browser: https://github.com/login/device' >&2
    i=0
    while [ ! -f "$FAKE_GH_CONTINUE" ] && [ $i -lt 100 ]; do sleep 0.1; i=$((i+1)); done
    [ -f "$FAKE_GH_CONTINUE" ] || exit 9
    if [ -n "$FAKE_LOGIN_FAILS" ]; then echo 'failed to authenticate: the code expired' >&2; exit 1; fi
    echo '! Authentication credentials saved in plain text' >&2
    touch "$FAKE_SIGNED_IN"
    exit 0;;
  "api user") echo someone; exit 0;;
  "repo list") echo '[{"nameWithOwner":"someone/project","isPrivate":false,"isFork":false,"diskUsage":10}]'; exit 0;;
  "auth setup-git") exit 0;;
  "repo clone") mkdir "$4"; exit 0;;
esac
exit 0
)";

const QByteArray kRpm = R"(#!/bin/sh
echo "rpm $*" >> "$FAKE_LOG"
if [ -n "$FAKE_NO_GH" ]; then echo git; echo 'package gh is not installed'; exit 1; fi
echo git; echo gh
)";

const QByteArray kZypper = R"(#!/bin/sh
echo "zypper $*" >> "$FAKE_LOG"
exit 0
)";

const QByteArray kSudo = R"(#!/bin/sh
echo "sudo $*" >> "$FAKE_LOG"
case " $* " in *" -v "*) exit 0;; esac
while [ $# -gt 0 ]; do case "$1" in -n|-A) shift;; --) shift; break;; *) break;; esac; done
exec "$@"
)";

} // namespace

class TstGithub : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_bin, m_log, m_state, m_savedPath;
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
    QStringList calls() const
    {
        QFile log(m_log);
        if (!log.open(QIODevice::ReadOnly))
            return {};
        return QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    int worker(const QStringList &args, QByteArray *out = nullptr)
    {
        QProcess p;
        p.setProcessEnvironment(m_env);
        p.start(QStringLiteral(GROUNDWORK_BIN), QStringList{QStringLiteral("--worker")} + args);
        p.waitForFinished(30000);
        if (out)
            *out = p.readAllStandardOutput();
        return p.exitCode();
    }
    QString choices(const QString &folder, const QJsonArray &repos)
    {
        const QString path = m_dir->filePath(QStringLiteral("github-choices.json"));
        writeFile(path, QJsonDocument(QJsonObject{{"folder", folder}, {"repos", repos}}).toJson());
        return path;
    }
    QByteArray everythingWritten() const
    {
        QByteArray all;
        QDirIterator it(m_state, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile f(it.next());
            if (f.open(QIODevice::ReadOnly))
                all += f.readAll();
        }
        return all;
    }

private slots:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_bin = m_dir->filePath(QStringLiteral("bin"));
        m_log = m_dir->filePath(QStringLiteral("calls.log"));
        m_state = m_dir->filePath(QStringLiteral("state"));
        writeFile(m_bin + QStringLiteral("/gh"), kGh, true);
        writeFile(m_bin + QStringLiteral("/rpm"), kRpm, true);
        writeFile(m_bin + QStringLiteral("/zypper"), kZypper, true);
        writeFile(m_bin + QStringLiteral("/sudo"), kSudo, true);
        writeFile(m_dir->filePath(QStringLiteral("root/etc/os-release")), "ID=opensuse-tumbleweed\n");
        m_env = QProcessEnvironment();
        m_env.insert(QStringLiteral("PATH"), m_bin + QStringLiteral(":/usr/bin:/bin"));
        m_env.insert(QStringLiteral("HOME"), m_dir->path());
        m_env.insert(QStringLiteral("GROUNDWORK_ROOT"), m_dir->filePath(QStringLiteral("root")));
        m_env.insert(QStringLiteral("XDG_STATE_HOME"), m_state);
        m_env.insert(QStringLiteral("FAKE_LOG"), m_log);
        m_env.insert(QStringLiteral("FAKE_SIGNED_IN"), m_dir->filePath(QStringLiteral("signed-in")));
        // The same for what this process starts itself.
        m_savedPath = qEnvironmentVariable("PATH");
        qputenv("PATH", m_env.value(QStringLiteral("PATH")).toUtf8());
        qputenv("FAKE_LOG", m_log.toUtf8());
        qputenv("XDG_STATE_HOME", m_state.toUtf8());
        qputenv("FAKE_SIGNED_IN", m_env.value(QStringLiteral("FAKE_SIGNED_IN")).toUtf8());
        qputenv("FAKE_GH_CONTINUE", m_dir->filePath(QStringLiteral("continue")).toUtf8());
        qunsetenv("FAKE_LOGIN_FAILS");
        qunsetenv("FAKE_NO_GH");
    }
    void cleanup() { qputenv("PATH", m_savedPath.toUtf8()); }

    // Spec 4.3's table.
    void theCodeIsFound_data()
    {
        QTest::addColumn<QByteArray>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("clipboard") << QByteArray("! One-time code (ABCD-1234) copied to clipboard") << "ABCD-1234";
        QTest::newRow("no clipboard") << QByteArray("First copy your one-time code: WXYZ-9876") << "WXYZ-9876";
        QTest::newRow("the address") << QByteArray("Open this URL to continue in your web browser: https://github.com/login/device") << "";
    }
    void theCodeIsFound()
    {
        QFETCH(QByteArray, input);
        QFETCH(QString, expected);
        QCOMPARE(gw::oneTimeCode(input), expected);
    }

    void signingInIsAsTheSpecSays()
    {
        QCOMPARE(gw::signInArguments(), QStringList({"auth", "login", "--hostname", "github.com", "--git-protocol", "https", "--web"}));
    }

    // What gh said is shown without the code and the address.
    void signInWordsLeaveOutTheCode()
    {
        const QString words = gw::signInWords("! One-time code (ABCD-1234) copied to clipboard\n"
                                              "Open this URL to continue in your web browser: https://github.com/login/device\n"
                                              "failed to authenticate: the code expired\n");
        QCOMPARE(words, QStringLiteral("failed to authenticate: the code expired"));
    }

    // Spec 4.4's table, and the same pattern's other edges.
    void repositoryNames_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("ok");
        QTest::newRow("ordinary") << "someone/my-project.git2" << true;
        QTest::newRow("a dot first") << "someone/.github" << true;
        QTest::newRow("a dash first") << "someone/-oUpload" << false;
        QTest::newRow("dot dot") << "someone/.." << false;
        QTest::newRow("dot") << "someone/." << false;
        QTest::newRow("a space") << "someone/a b" << false;
        QTest::newRow("no owner") << "project" << false;
        QTest::newRow("two slashes") << "someone/a/b" << false;
        QTest::newRow("a line break") << "someone/a\nb" << false;
        // gh would read this as an option: the name goes first on its line.
        QTest::newRow("an owner with a dash first") << "-oUpload/project" << false;
        QTest::newRow("an owner of dots") << "../project" << false;
    }
    void repositoryNames()
    {
        QFETCH(QString, name);
        QFETCH(bool, ok);
        QCOMPARE(gw::isRepositoryName(name), ok);
    }

    // INV-2: a fork starts off; a private one, like any other, starts on.
    void forksStartOff()
    {
        const auto list = gw::parseRepositories(R"([
            {"nameWithOwner":"someone/mine","isPrivate":false,"isFork":false,"diskUsage":1},
            {"nameWithOwner":"someone/secret","isPrivate":true,"isFork":false,"diskUsage":2},
            {"nameWithOwner":"someone/theirs","isPrivate":false,"isFork":true,"diskUsage":3},
            {"nameWithOwner":"someone/-bad","isPrivate":false,"isFork":false,"diskUsage":4}])");
        QVERIFY(list);
        QCOMPARE(list->size(), 3);
        QVERIFY(list->at(0).startsOn());
        QVERIFY(list->at(1).startsOn());
        QVERIFY(list->at(1).isPrivate);
        QVERIFY(!list->at(2).startsOn());
        QCOMPARE(list->at(2).diskKiB, qint64(3));
        QCOMPARE(list->at(2).name(), QStringLiteral("theirs"));
    }

    void aDamagedListIsNotAnEmptyOne()
    {
        QVERIFY(!gw::parseRepositories("gh: unknown field"));
        QVERIFY(!gw::parseRepositories("{}"));
        QCOMPARE(gw::parseRepositories("[]")->size(), 0);
    }

    // Spec 4.6.
    void whichFolder_data()
    {
        QTest::addColumn<QString>("folder");
        QTest::addColumn<bool>("usable");
        const QString home = QDir::homePath();
        QTest::newRow("relative") << "Projects" << false;
        QTest::newRow("not clean") << QDir::tempPath() + "/a/../b" << false;
        QTest::newRow("the root") << "/" << false;
        QTest::newRow("a system folder") << "/usr/share/projects" << false;
        QTest::newRow("run") << "/run/user/projects" << false;
        QTest::newRow("a dot-folder at home") << home + "/.config/projects" << false;
        QTest::newRow("the home folder") << home << true;
        QTest::newRow("new, in the temporary folder") << QDir::tempPath() + "/grnd-no-such/deep/er" << true;
    }
    void whichFolder()
    {
        QFETCH(QString, folder);
        QFETCH(bool, usable);
        QCOMPARE(gw::githubFolderProblem(folder).isEmpty(), usable);
    }
    void aFileOrAnUnwritableParentIsRefused()
    {
        const QString file = m_dir->filePath(QStringLiteral("a-file"));
        writeFile(file, "x");
        QVERIFY(!gw::githubFolderProblem(file).isEmpty());
        QVERIFY(!gw::githubFolderProblem(file + QStringLiteral("/below")).isEmpty());
        const QString locked = m_dir->filePath(QStringLiteral("locked"));
        QVERIFY(QDir().mkpath(locked));
        QVERIFY(QFile::setPermissions(locked, QFile::ReadOwner | QFile::ExeOwner));
        QVERIFY(!gw::githubFolderProblem(locked + QStringLiteral("/new")).isEmpty());
        QVERIFY(QFile::setPermissions(locked, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QCOMPARE(gw::nearestExistingFolder(locked + QStringLiteral("/new/deeper")), locked);
    }

    void theChoicesRoundTrip()
    {
        const QString folder = m_dir->filePath(QStringLiteral("Projects"));
        QString why;
        const auto read = gw::readGithubChoices(gw::githubChoicesJson({folder, {"someone/a", "someone/b"}}), &why);
        QVERIFY2(read, qPrintable(why));
        QCOMPARE(read->folder, folder);
        QCOMPARE(read->repos, QStringList({"someone/a", "someone/b"}));
    }

    // INV-3: one bad name in each position, a shared last part, and a bad
    // folder each refuse the run before any step, and gh never runs.
    void aBadNameRefusesTheRun_data()
    {
        QTest::addColumn<QStringList>("repos");
        QTest::newRow("first") << QStringList{"someone/-oUpload", "someone/a", "someone/b"};
        QTest::newRow("middle") << QStringList{"someone/a", "someone/..", "someone/b"};
        QTest::newRow("last") << QStringList{"someone/a", "someone/b", "someone/a b"};
        QTest::newRow("an owner as an option") << QStringList{"-oUpload/a"};
        QTest::newRow("the same last part") << QStringList{"someone/a", "other/a"};
        QTest::newRow("none") << QStringList{};
    }
    void aBadNameRefusesTheRun()
    {
        QFETCH(QStringList, repos);
        QJsonArray list;
        for (const QString &repo : repos)
            list << repo;
        const QString folder = m_dir->filePath(QStringLiteral("Projects"));
        QByteArray out;
        QCOMPARE(worker({QStringLiteral("--copy-github"), choices(folder, list)}, &out), 2);
        QVERIFY2(out.contains("@@SETUP_REFUSED@@"), out.constData());
        QVERIFY(!out.contains("STEP_BEGIN"));
        QCOMPARE(calls(), QStringList());
        QVERIFY(!QFile::exists(folder)); // nothing was made either
    }
    void aBadFolderRefusesTheRun()
    {
        QCOMPARE(worker({QStringLiteral("--copy-github"), choices(QStringLiteral("/usr/projects"), {"someone/a"})}), 2);
        QCOMPARE(calls(), QStringList());
        QCOMPARE(worker({QStringLiteral("--copy-github"), m_dir->filePath(QStringLiteral("no-such-file"))}), 2);
        QCOMPARE(calls(), QStringList());
    }

    // INV-6, on the step's arguments; only a real git can show it obeys them.
    void aCopyNeverAsks()
    {
        const QList<gw::Step> steps = gw::githubCopySteps(QStringLiteral("/data/Projects"), QStringLiteral("someone/project"));
        QCOMPARE(steps.size(), 2);
        QCOMPARE(steps[0].argv, QStringList({"env", "GIT_TERMINAL_PROMPT=0", "GIT_ASKPASS=", "SSH_ASKPASS=", "gh", "repo", "clone",
                                             "someone/project", "/data/Projects/.project.partial"}));
        QCOMPARE(steps[1].argv, QStringList({"mv", "-n", "-T", "--", "/data/Projects/.project.partial", "/data/Projects/project"}));
    }

    // INV-7.
    void onlyTheInstallIsRoot()
    {
        const gw::Step install = gw::githubToolsStep();
        QVERIFY(install.needsRoot);
        QCOMPARE(install.tool, gw::Step::Tool::Zypper);
        QCOMPARE(install.argv, QStringList({"zypper", "-n", "install", "git", "gh"}));
        QList<gw::Step> copies = gw::githubCopySteps(QStringLiteral("/data/P"), QStringLiteral("someone/a"));
        copies << gw::makeGithubFolderStep(QStringLiteral("/data/P")) << gw::setupGitStep();
        for (const gw::Step &step : std::as_const(copies))
            QVERIFY2(!step.needsRoot, qPrintable(step.argv.join(QLatin1Char(' '))));
    }

    void theSurveyStopsAtTheFirstGap()
    {
        const gw::CheckContext context{gw::FileReader()};
        qputenv("FAKE_NO_GH", "1");
        gw::GithubSurvey survey = gw::surveyGithub(context);
        QCOMPARE(survey.missing, QStringList{"gh"});
        QVERIFY(!calls().join(QLatin1Char('\n')).contains(QLatin1String("gh auth")));
        qunsetenv("FAKE_NO_GH");

        survey = gw::surveyGithub(context);
        QVERIFY(survey.missing.isEmpty());
        QVERIFY(!survey.signedIn);
        QVERIFY(survey.repositories.isEmpty());

        writeFile(m_env.value(QStringLiteral("FAKE_SIGNED_IN")), "");
        survey = gw::surveyGithub(context);
        QVERIFY(survey.signedIn);
        QCOMPARE(survey.account, QStringLiteral("someone"));
        QCOMPARE(survey.repositories.size(), 1);
        QVERIFY(!survey.listDamaged);
        QVERIFY(calls().contains(QStringLiteral("gh repo list --limit 1000 --json nameWithOwner,isPrivate,isFork,diskUsage")));
    }

    // The job's page: the code shown large, copied and opened from buttons,
    // and on gh's exit 0 the page is done.
    void theSignInPageShowsTheCode()
    {
        gw::GithubChoice choice;
        choice.survey = [] { return gw::surveyGithub(gw::CheckContext(gw::FileReader())); };
        QWizard wizard;
        auto *page = new gw::GithubSignInPage(&choice);
        wizard.addPage(page);
        wizard.addPage(new QWizardPage);
        wizard.show();
        QVERIFY(!page->isComplete());
        auto *start = page->findChild<QPushButton *>(QStringLiteral("signIn"));
        auto *code = page->findChild<QLabel *>(QStringLiteral("oneTimeCode"));
        QVERIFY(start && code);
        QVERIFY(!code->isVisible());
        start->click();
        QTRY_COMPARE(code->text(), QStringLiteral("ABCD-1234"));
        QVERIFY(code->isVisible());
        QVERIFY(!start->isEnabled());
        QVERIFY(calls().contains(QStringLiteral("gh auth login --hostname github.com --git-protocol https --web")));
        writeFile(qEnvironmentVariable("FAKE_GH_CONTINUE"), "");
        QTRY_VERIFY_WITH_TIMEOUT(wizard.currentPage() != page, 10000);
        QVERIFY(choice.found.signedIn);
        QCOMPARE(choice.signInNote, QStringLiteral("! Authentication credentials saved in plain text"));
    }

    // Spec 6: the code expired or was declined; the button starts afresh.
    void aFailedSignInSaysWhyAndCanBeTriedAgain()
    {
        qputenv("FAKE_LOGIN_FAILS", "1");
        writeFile(qEnvironmentVariable("FAKE_GH_CONTINUE"), "");
        gw::GithubChoice choice;
        QWizard wizard;
        auto *page = new gw::GithubSignInPage(&choice);
        wizard.addPage(page);
        wizard.show();
        auto *start = page->findChild<QPushButton *>(QStringLiteral("signIn"));
        auto *status = page->findChild<QLabel *>(QStringLiteral("signInStatus"));
        QVERIFY(start && status);
        start->click();
        QTRY_VERIFY_WITH_TIMEOUT(start->isEnabled(), 10000);
        QVERIFY2(status->text().contains(QLatin1String("the code expired")), qPrintable(status->text()));
        QVERIFY(!page->isComplete());
    }

    // Leaving the page ends the waiting gh (spec 4.3), which stored nothing.
    void leavingThePageEndsTheSignIn()
    {
        gw::GithubChoice choice;
        QWizard wizard;
        auto *first = new QWizardPage;
        wizard.addPage(first);
        auto *page = new gw::GithubSignInPage(&choice);
        wizard.addPage(page);
        wizard.show();
        wizard.next();
        page->findChild<QPushButton *>(QStringLiteral("signIn"))->click();
        auto *code = page->findChild<QLabel *>(QStringLiteral("oneTimeCode"));
        QTRY_COMPARE(code->text(), QStringLiteral("ABCD-1234"));
        wizard.back();
        QTRY_VERIFY(!page->isSigningIn());
        QVERIFY(!QFile::exists(qEnvironmentVariable("FAKE_SIGNED_IN")));
    }

    // INV-1: every command the job can run, none asking for the token.
    void theTokenIsNeverRead()
    {
        // Sign in, read the list, copy.
        writeFile(qEnvironmentVariable("FAKE_GH_CONTINUE"), "");
        gw::GithubChoice choice;
        choice.survey = [] { return gw::surveyGithub(gw::CheckContext(gw::FileReader())); };
        QWizard wizard;
        auto *page = new gw::GithubSignInPage(&choice);
        wizard.addPage(page);
        wizard.addPage(new QWizardPage);
        wizard.show();
        page->findChild<QPushButton *>(QStringLiteral("signIn"))->click();
        QTRY_VERIFY_WITH_TIMEOUT(wizard.currentPage() != page, 10000);
        const QString folder = m_dir->filePath(QStringLiteral("Projects"));
        QCOMPARE(worker({QStringLiteral("--copy-github"), choices(folder, {"someone/project"})}), 0);
        const QStringList ran = calls();
        QVERIFY(ran.contains(QStringLiteral("gh repo clone someone/project ") + folder + QStringLiteral("/.project.partial")));
        for (const QString &call : ran) {
            QVERIFY2(!call.contains(QLatin1String("token")), qPrintable(call));
            QVERIFY2(!call.contains(QLatin1String("hosts.yml")), qPrintable(call));
        }
    }

    // INV-1: the code reaches the page and nothing else.
    void theCodeIsNotLogged()
    {
        QVERIFY(QDir().mkpath(m_state + QStringLiteral("/groundwork/logs")));
        writeFile(m_state + QStringLiteral("/groundwork/logs/earlier.log"), "a line\n");
        gw::GithubChoice choice;
        QWizard wizard;
        auto *page = new gw::GithubSignInPage(&choice);
        wizard.addPage(page);
        wizard.show();
        page->findChild<QPushButton *>(QStringLiteral("signIn"))->click();
        QTRY_COMPARE(page->findChild<QLabel *>(QStringLiteral("oneTimeCode"))->text(), QStringLiteral("ABCD-1234"));
        const QString folder = m_dir->filePath(QStringLiteral("Projects"));
        QByteArray out;
        QCOMPARE(worker({QStringLiteral("--copy-github"), choices(folder, {"someone/project"})}, &out), 0);
        QVERIFY(!out.contains("ABCD-1234"));
        QVERIFY(!everythingWritten().contains("ABCD-1234"));
    }
};

QTEST_MAIN(TstGithub)
#include "tst_github.moc"
