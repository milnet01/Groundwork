// The emergency keyboard escape (GRND-0053), against a fixture root
// holding the kernel's live setting.
#include "items/sysrqitem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstSysrqItem : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_root;

    void setting(const QByteArray &value)
    {
        const QString path = m_root->filePath(QStringLiteral("proc/sys/kernel/sysrq"));
        QVERIFY(QDir().mkpath(QFileInfo(path).path()));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(value);
    }
    gw::CheckState state() const
    {
        return gw::SysrqItem().check(gw::CheckContext(gw::FileReader(m_root->path()))).state;
    }

private slots:
    void init() { m_root = std::make_unique<QTemporaryDir>(); }

    // 1 switches every key on; a mask counts where it holds the keyboard
    // (4), signals (64), sync (16), read-only remount (32) and reboot (128).
    void doneOnlyWhenEveryEscapeKeyWorks()
    {
        setting("1\n");
        QCOMPARE(state(), gw::CheckState::Done);
        setting("244\n");
        QCOMPARE(state(), gw::CheckState::Done);
        setting("510\n");
        QCOMPARE(state(), gw::CheckState::Done);
        setting("184\n"); // the distribution's own: no R, E, I or F
        QCOMPARE(state(), gw::CheckState::NotDone);
        setting("0\n");
        QCOMPARE(state(), gw::CheckState::NotDone);
    }

    void anUnreadableSettingCannotBeTold()
    {
        QCOMPARE(state(), gw::CheckState::CouldNotTell);
        setting("lots\n");
        QCOMPARE(state(), gw::CheckState::CouldNotTell);
    }

    // /etc/sysctl.d may not exist on a fresh system. The file sorts after the distribution's 50-default.conf, which sets
    // 184, so it wins at every boot; sysctl then applies it now.
    void applyingWritesTheSettingAndLoadsIt()
    {
        const QList<gw::Step> steps = gw::SysrqItem().applySteps({}, gw::CheckContext(gw::FileReader(m_root->path())));
        QCOMPARE(steps.size(), 3);
        QCOMPARE(steps[0].argv, (QStringList{QStringLiteral("mkdir"), QStringLiteral("-p"), QStringLiteral("/etc/sysctl.d")}));
        QCOMPARE(steps[1].argv.value(1), QStringLiteral("of=/etc/sysctl.d/99-groundwork-sysrq.conf"));
        QVERIFY(steps[1].input.contains("\nkernel.sysrq = 1\n"));
        QCOMPARE(steps[2].argv,
                 (QStringList{QStringLiteral("sysctl"), QStringLiteral("-p"),
                              QStringLiteral("/etc/sysctl.d/99-groundwork-sysrq.conf")}));
        for (const gw::Step &s : steps)
            QVERIFY(s.needsRoot);
    }
};

QTEST_MAIN(TstSysrqItem)
#include "tst_sysrqitem.moc"
