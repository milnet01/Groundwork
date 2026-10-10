// Ants Terminal, set as the terminal (GRND-0061), against a fake rpm first
// on PATH and a fixture root holding os-release, the repositories, KDE's
// kwriteconfig6 and the user's kdeglobals.
#include "items/antsterminalitem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
const QString kRepo = QStringLiteral("https://download.opensuse.org/repositories/home:/milnet:/ants-terminal/");
const QString kDesktop = QStringLiteral("za.co.antsprojectshub.AntsTerminal.desktop");

void write(const QString &path, const QByteArray &text, bool executable = false)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).path()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text);
    if (executable)
        f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
}

QStringList argvs(const QList<gw::Step> &steps)
{
    QStringList out;
    for (const gw::Step &s : steps)
        out << (s.needsRoot ? QStringLiteral("root: ") : QStringLiteral("user: ")) + s.argv.join(QLatin1Char(' '));
    return out;
}
} // namespace

class TstAntsTerminalItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin;
    QByteArray m_oldPath, m_oldConfig;
    std::unique_ptr<QTemporaryDir> m_root;

    void rooted(const QString &path, const QByteArray &text) { write(m_root->filePath(path), text); }
    void installed(bool yes) { qputenv("FAKE_RPM", yes ? "0" : "1"); }
    void tumbleweed() { rooted(QStringLiteral("etc/os-release"), "ID=\"opensuse-tumbleweed\"\nVERSION_ID=\"20261008\"\n"); }
    void kde() { rooted(QStringLiteral("usr/bin/kwriteconfig6"), ""); }
    void terminal(const QByteArray &service)
    {
        rooted(QStringLiteral("cfg/kdeglobals"), "[General]\nTerminalApplication=x\nTerminalService=" + service
                                                    + "\n\n[KDE]\nTerminalService=other.desktop\n");
    }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }
    gw::CheckState state() const { return gw::AntsTerminalItem().check(context()).state; }
    QList<gw::Step> steps(const QByteArray &osRelease) const
    {
        return gw::AntsTerminalItem().applySteps(gw::parseOsRelease(osRelease), context());
    }

private slots:
    void initTestCase()
    {
        write(m_bin.filePath(QStringLiteral("rpm")), "#!/bin/sh\nexit \"$FAKE_RPM\"\n", true);
        m_oldPath = qgetenv("PATH");
        m_oldConfig = qgetenv("XDG_CONFIG_HOME");
        qputenv("PATH", m_bin.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", "/cfg");
    }
    void cleanupTestCase()
    {
        qputenv("PATH", m_oldPath);
        qputenv("XDG_CONFIG_HOME", m_oldConfig);
    }
    void init() { m_root = std::make_unique<QTemporaryDir>(); }

    // Its repository builds for Tumbleweed and Leap 16.0 only.
    void notNeededWhereItIsNotBuilt_data()
    {
        QTest::addColumn<QByteArray>("osRelease");
        QTest::newRow("Slowroll") << QByteArray("ID=opensuse-slowroll\nVERSION_ID=20261001\n");
        QTest::newRow("Leap 16.1") << QByteArray("ID=opensuse-leap\nVERSION_ID=\"16.1\"\n");
    }
    void notNeededWhereItIsNotBuilt()
    {
        QFETCH(QByteArray, osRelease);
        rooted(QStringLiteral("etc/os-release"), osRelease);
        installed(false);
        QCOMPARE(state(), gw::CheckState::NotNeeded);
    }

    void withoutKdeInstallingIsEnough()
    {
        tumbleweed();
        installed(false);
        QCOMPARE(state(), gw::CheckState::NotDone);
        installed(true);
        QCOMPARE(state(), gw::CheckState::Done);
    }

    // On KDE it must also be the terminal the desktop opens; only
    // [General] counts.
    void onKdeItMustBeTheDesktopsTerminal()
    {
        rooted(QStringLiteral("etc/os-release"), "ID=opensuse-leap\nVERSION_ID=\"16.0\"\n");
        kde();
        installed(true);
        QCOMPARE(state(), gw::CheckState::NotDone);
        terminal("org.kde.konsole.desktop");
        QCOMPARE(state(), gw::CheckState::NotDone);
        terminal(kDesktop.toUtf8());
        QCOMPARE(state(), gw::CheckState::Done);
    }

    void applyingAddsTheRepositoryInstallsAndSetsTheTerminal()
    {
        kde();
        const QStringList expected = {
            QStringLiteral("root: zypper -n addrepo -f ") + kRepo + QStringLiteral("openSUSE_Leap_16.0/ ants-terminal"),
            QStringLiteral("root: zypper -n --gpg-auto-import-keys refresh ants-terminal"),
            QStringLiteral("root: zypper -n install ants-terminal"),
            QStringLiteral("user: kwriteconfig6 --file kdeglobals --group General --key TerminalApplication ants-terminal"),
            QStringLiteral("user: kwriteconfig6 --file kdeglobals --group General --key TerminalService ") + kDesktop,
        };
        QCOMPARE(argvs(steps("ID=opensuse-leap\nVERSION_ID=\"16.0\"\n")), expected);
    }

    // A repository already pointing there is used as it is; without KDE
    // there is no terminal to set.
    void applyingUsesARepositoryAlreadyThere()
    {
        rooted(QStringLiteral("etc/zypp/repos.d/mine.repo"),
               "[mine]\nenabled=1\nbaseurl=" + kRepo.toUtf8() + "openSUSE_Tumbleweed/\n");
        const QList<gw::Step> s = steps("ID=opensuse-tumbleweed\nVERSION_ID=20261008\n");
        QCOMPARE(argvs(s), (QStringList{QStringLiteral("root: zypper -n --gpg-auto-import-keys refresh mine"),
                                        QStringLiteral("root: zypper -n install ants-terminal")}));
        QCOMPARE(s[1].tool, gw::Step::Tool::Zypper);
    }
};

QTEST_MAIN(TstAntsTerminalItem)
#include "tst_antsterminalitem.moc"
