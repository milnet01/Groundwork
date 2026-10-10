// Gaming additions (GRND-0063), against a fake rpm first on PATH that
// knows the packages listed in FAKE_INSTALLED, and a fixture root holding
// SELinux's enforce switch.
#include "items/gamingextrasitem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
void write(const QString &path, const QByteArray &text, bool executable = false)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).path()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text);
    if (executable)
        f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
}
} // namespace

class TstGamingExtrasItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin;
    QByteArray m_oldPath;
    std::unique_ptr<QTemporaryDir> m_root;

    void installed(const QByteArray &packages) { qputenv("FAKE_INSTALLED", " " + packages + " "); }
    void selinux(const QByteArray &enforce) { write(m_root->filePath(QStringLiteral("sys/fs/selinux/enforce")), enforce); }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }
    gw::CheckState state() const { return gw::GamingExtrasItem().check(context()).state; }
    QStringList installArgv() const
    {
        const QList<gw::Step> steps = gw::GamingExtrasItem().applySteps({}, context());
        return steps.size() == 1 && steps[0].needsRoot && steps[0].tool == gw::Step::Tool::Zypper ? steps[0].argv
                                                                                                    : QStringList();
    }

private slots:
    void initTestCase()
    {
        write(m_bin.filePath(QStringLiteral("rpm")),
              "#!/bin/sh\ncase \"$FAKE_INSTALLED\" in *\" $2 \"*) exit 0;; esac\nexit 1\n", true);
        m_oldPath = qgetenv("PATH");
        qputenv("PATH", m_bin.path().toUtf8() + ":/usr/bin:/bin");
    }
    void cleanupTestCase() { qputenv("PATH", m_oldPath); }
    void init() { m_root = std::make_unique<QTemporaryDir>(); }

    void doneWhenControllersAndGameModeAreInstalled()
    {
        installed("steam-devices");
        QCOMPARE(state(), gw::CheckState::NotDone);
        installed("steam-devices gamemoded");
        QCOMPARE(state(), gw::CheckState::Done);
    }

    // SELinux's gaming policy is needed only where SELinux is enforcing.
    void selinuxEnforcingNeedsItsGamingPolicy()
    {
        installed("steam-devices gamemoded");
        selinux("0");
        QCOMPARE(state(), gw::CheckState::Done);
        selinux("1");
        QCOMPARE(state(), gw::CheckState::NotDone);
        installed("steam-devices gamemoded selinux-policy-targeted-gaming");
        QCOMPARE(state(), gw::CheckState::Done);
    }

    void applyingInstallsWhatIsNeededHere()
    {
        QCOMPARE(installArgv(), (QStringList{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"),
                                             QStringLiteral("steam-devices"), QStringLiteral("gamemoded")}));
        selinux("1");
        QCOMPARE(installArgv(), (QStringList{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"),
                                             QStringLiteral("steam-devices"), QStringLiteral("gamemoded"),
                                             QStringLiteral("selinux-policy-targeted-gaming")}));
    }
};

QTEST_MAIN(TstGamingExtrasItem)
#include "tst_gamingextrasitem.moc"
