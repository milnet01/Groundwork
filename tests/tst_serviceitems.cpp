// The firewall and SSH items, against a fake systemctl that answers per
// unit from files, with the codes measured on 2026-10-02 (items/service.h).
#include "items/catalogue.h"
#include "items/firewallitem.h"
#include "items/sshitem.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
const QByteArray kSystemctl = R"(#!/bin/sh
# systemctl is-enabled|is-active UNIT, answered from $FAKE_UNITS/UNIT.enabled
# and UNIT.active; a unit with no files does not exist.
cmd=$1; unit=$2
if [ ! -f "$FAKE_UNITS/$unit.enabled" ]; then
  [ "$cmd" = is-enabled ] && echo not-found || echo inactive; exit 4
fi
case $cmd in
  is-enabled) v=$(cat "$FAKE_UNITS/$unit.enabled"); echo "$v"; [ "$v" = enabled ] && exit 0; exit 1;;
  is-active)  v=$(cat "$FAKE_UNITS/$unit.active"); echo "$v"; [ "$v" = active ] && exit 0; exit 3;;
esac
)";
} // namespace

class TstServiceItems : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin, m_root;
    std::unique_ptr<QTemporaryDir> m_units;
    QByteArray m_oldPath;

    void unit(const QString &name, const QByteArray &enabled, const QByteArray &active)
    {
        QFile e(m_units->filePath(name + QStringLiteral(".enabled")));
        QVERIFY(e.open(QIODevice::WriteOnly));
        e.write(enabled);
        QFile a(m_units->filePath(name + QStringLiteral(".active")));
        QVERIFY(a.open(QIODevice::WriteOnly));
        a.write(active);
    }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root.path())); }
    static gw::SystemIdentity tumbleweed() { return gw::parseOsRelease("ID=opensuse-tumbleweed\n"); }

private slots:
    void initTestCase()
    {
        QFile f(m_bin.filePath(QStringLiteral("systemctl")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kSystemctl);
        f.close();
        QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
        m_oldPath = qgetenv("PATH");
        qputenv("PATH", m_bin.path().toUtf8() + ":/usr/bin:/bin");
    }
    void cleanupTestCase() { qputenv("PATH", m_oldPath); }
    void init()
    {
        m_units = std::make_unique<QTemporaryDir>();
        qputenv("FAKE_UNITS", m_units->path().toUtf8());
    }

    void firewallOnIsDone()
    {
        unit(QStringLiteral("firewalld"), "enabled", "active");
        QCOMPARE(gw::FirewallItem().check(context()).state, gw::CheckState::Done);
        const auto steps = gw::FirewallItem().applySteps(tumbleweed(), context());
        QCOMPARE(steps.size(), 1); // installed: no install step
        QCOMPARE(steps[0].argv, QStringList({"systemctl", "enable", "--now", "firewalld"}));
    }

    void firewallOffIsNotDone()
    {
        unit(QStringLiteral("firewalld"), "disabled", "inactive");
        QCOMPARE(gw::FirewallItem().check(context()).state, gw::CheckState::NotDone);
    }

    void missingFirewallIsInstalledFirst()
    {
        QCOMPARE(gw::FirewallItem().check(context()).state, gw::CheckState::NotDone);
        const auto steps = gw::FirewallItem().applySteps(tumbleweed(), context());
        QCOMPARE(steps.size(), 2);
        QCOMPARE(steps[0].argv, QStringList({"zypper", "-n", "install", "firewalld"}));
        QCOMPARE(steps[0].tool, gw::Step::Tool::Zypper);
    }

    void noServiceManagerCouldNotTell()
    {
        qputenv("PATH", QByteArray("/nonexistent"));
        QCOMPARE(gw::FirewallItem().check(context()).state, gw::CheckState::CouldNotTell);
        qputenv("PATH", m_bin.path().toUtf8() + ":/usr/bin:/bin");
    }

    void sshOffIsNotDoneAndOnIsDone()
    {
        unit(QStringLiteral("sshd"), "disabled", "inactive");
        QCOMPARE(gw::SshItem().check(context()).state, gw::CheckState::NotDone);
        unit(QStringLiteral("sshd"), "enabled", "active");
        QCOMPARE(gw::SshItem().check(context()).state, gw::CheckState::Done);
    }

    void sshIsLetThroughARunningFirewall()
    {
        unit(QStringLiteral("sshd"), "disabled", "inactive");
        unit(QStringLiteral("firewalld"), "enabled", "active");
        const auto steps = gw::SshItem().applySteps(tumbleweed(), context());
        QCOMPARE(steps.size(), 3);
        QCOMPARE(steps[1].argv, QStringList({"firewall-cmd", "--permanent", "--add-service=ssh"}));
        QCOMPARE(steps[2].argv, QStringList({"firewall-cmd", "--reload"}));
    }

    void sshWithoutAFirewallOrServerInstallsAndEnables()
    {
        const auto steps = gw::SshItem().applySteps(tumbleweed(), context());
        QCOMPARE(steps.size(), 2);
        QCOMPARE(steps[0].argv, QStringList({"zypper", "-n", "install", "openssh-server"}));
        QCOMPARE(steps[1].argv, QStringList({"systemctl", "enable", "--now", "sshd"}));
    }

    void neitherStartsSwitchedOnAndTheCatalogueStaysOrdered()
    {
        QCOMPARE(gw::FirewallItem().level(), gw::Level::SystemSetup);
        QCOMPARE(gw::SshItem().level(), gw::Level::Configuration);
        QVERIFY(gw::catalogue().find(QStringLiteral("firewall")));
        QVERIFY(gw::catalogue().find(QStringLiteral("ssh")));
        QVERIFY(gw::catalogue().orderProblems().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstServiceItems)
#include "tst_serviceitems.moc"
