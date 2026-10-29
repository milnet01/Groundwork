// Claude Code (GRND-0082), against a fixture root: done when claude is on
// PATH or in ~/.local/bin; applying imports the built-in key, adds
// Anthropic's repository unless one is there, and installs the package.
#include "items/catalogue.h"
#include "items/claudecodeitem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
const QString kRepo = QStringLiteral("https://downloads.claude.ai/claude-code/rpm/stable");
const QString kKeyFile = QStringLiteral("/etc/pki/rpm-gpg/claude-code.asc");

void write(const QString &path, const QByteArray &text)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).path()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text);
}

QStringList argvs(const QList<gw::Step> &steps)
{
    QStringList out;
    for (const gw::Step &s : steps)
        out << (s.needsRoot ? QStringLiteral("root: ") : QStringLiteral("user: ")) + s.argv.join(QLatin1Char(' '));
    return out;
}
} // namespace

class TstClaudeCodeItem : public QObject
{
    Q_OBJECT

    QByteArray m_oldPath, m_oldHome;
    std::unique_ptr<QTemporaryDir> m_root;

    void rooted(const QString &path, const QByteArray &text = {}) { write(m_root->filePath(path), text); }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }
    gw::CheckState state() const { return gw::ClaudeCodeItem().check(context()).state; }
    QList<gw::Step> steps() const
    {
        return gw::ClaudeCodeItem().applySteps(gw::parseOsRelease("ID=opensuse-tumbleweed\n"), context());
    }

private slots:
    void initTestCase()
    {
        m_oldPath = qgetenv("PATH");
        m_oldHome = qgetenv("HOME");
        qputenv("PATH", "/usr/local/bin:/usr/bin");
        qputenv("HOME", "/home/u");
    }
    void cleanupTestCase()
    {
        qputenv("PATH", m_oldPath);
        qputenv("HOME", m_oldHome);
    }
    void init() { m_root = std::make_unique<QTemporaryDir>(); }

    void doneWhenClaudeIsOnPath()
    {
        QCOMPARE(state(), gw::CheckState::NotDone);
        rooted(QStringLiteral("usr/sbin/claude")); // not on PATH
        QCOMPARE(state(), gw::CheckState::NotDone);
        rooted(QStringLiteral("usr/bin/claude"));
        QCOMPARE(state(), gw::CheckState::Done);
    }

    // Anthropic's own installer puts it in ~/.local/bin, which a desktop
    // session's PATH may lack.
    void doneWhenAnthropicsInstallerPutItInTheHomeFolder()
    {
        rooted(QStringLiteral("home/u/.local/bin/claude"));
        QCOMPARE(state(), gw::CheckState::Done);
    }

    void applyingImportsTheKeyAddsTheRepositoryAndInstalls()
    {
        const QList<gw::Step> s = steps();
        QCOMPARE(argvs(s), (QStringList{
                               QStringLiteral("root: mkdir -p /etc/pki/rpm-gpg"),
                               QStringLiteral("root: dd of=") + kKeyFile + QStringLiteral(" status=none"),
                               QStringLiteral("root: rpm --import ") + kKeyFile,
                               QStringLiteral("root: zypper -n addrepo -f ") + kRepo + QStringLiteral(" claude-code"),
                               QStringLiteral("root: zypper -n refresh claude-code"),
                               QStringLiteral("root: zypper -n install claude-code"),
                           }));
        // The key written is the built-in one, never a downloaded one.
        QVERIFY(s[1].input.startsWith("-----BEGIN PGP PUBLIC KEY BLOCK-----"));
        QCOMPARE(s[2].tool, gw::Step::Tool::Generic);
        QCOMPARE(s[3].tool, gw::Step::Tool::Zypper);
    }

    void applyingUsesARepositoryAlreadyThere()
    {
        rooted(QStringLiteral("etc/zypp/repos.d/anthropic.repo"),
               "[anthropic]\nenabled=1\nbaseurl=" + kRepo.toUtf8() + "\n");
        const QStringList s = argvs(steps());
        QVERIFY2(!s.join(QLatin1Char('\n')).contains(QLatin1String("addrepo")), qPrintable(s.join(QLatin1Char('\n'))));
        QVERIFY(s.contains(QStringLiteral("root: zypper -n refresh anthropic")));
    }

    // GRND-0093: a repository the user switched off is switched on, never
    // added again under the alias it already holds.
    void applyingSwitchesOnARepositoryTurnedOff()
    {
        rooted(QStringLiteral("etc/zypp/repos.d/claude-code.repo"),
               "[claude-code]\nenabled=0\nbaseurl=" + kRepo.toUtf8() + "\n");
        const QStringList s = argvs(steps());
        QVERIFY2(!s.join(QLatin1Char('\n')).contains(QLatin1String("addrepo")), qPrintable(s.join(QLatin1Char('\n'))));
        QVERIFY(s.contains(QStringLiteral("root: zypper -n modifyrepo -e claude-code")));
        QVERIFY(s.contains(QStringLiteral("root: zypper -n refresh claude-code")));
    }

    // It joins the developer tools. No released Ants Terminal offers it
    // yet, so the row does not say so (GRND-0082, 2026-10-29).
    void inTheCatalogue()
    {
        const gw::Catalogue &all = gw::catalogue();
        const gw::Item *item = all.find(QStringLiteral("dev-claude-code"));
        QVERIFY(item);
        QVERIFY(item->installsPackages());
        QCOMPARE(item->level(), gw::Level::NiceToHave);
        QVERIFY(item->alsoOfferedBy().isEmpty());
    }
};

QTEST_MAIN(TstClaudeCodeItem)
#include "tst_claudecodeitem.moc"
