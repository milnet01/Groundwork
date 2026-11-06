// Hibernate instead of sleep (GRND-0068), against a fixture root holding
// the kernel's power files, the swap list and the sleep settings.
#include "items/catalogue.h"
#include "items/hibernateitem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstHibernateItem : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_root;

    void write(const QString &path, const QByteArray &text)
    {
        const QString full = m_root->filePath(path);
        QVERIFY(QDir().mkpath(QFileInfo(full).path()));
        QFile f(full);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
    }
    // The owner's machine (read 2026-11-06): 32 GiB of memory, a swap
    // partition slightly larger, and the kernel waking from it.
    void offerable()
    {
        write(QStringLiteral("sys/power/state"), "freeze mem disk\n");
        write(QStringLiteral("sys/power/resume"), "8:3\n");
        write(QStringLiteral("sys/class/block/sda3/dev"), "8:3\n");
        write(QStringLiteral("proc/swaps"), "Filename\t\t\t\tType\t\tSize\t\tUsed\t\tPriority\n"
                                            "/dev/sda3                               partition\t32781472\t378352\t\t-1\n");
        write(QStringLiteral("proc/meminfo"), "MemTotal:       32778056 kB\nMemFree:         1000 kB\n");
        write(QStringLiteral("sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c"),
              QByteArray("\x06\x00\x00\x00\x00", 5));
    }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }
    gw::CheckState state() const { return gw::HibernateItem().check(context()).state; }

private slots:
    void init() { m_root = std::make_unique<QTemporaryDir>(); }

    void notDoneOnTheOwnersMachineBeforeApplying()
    {
        offerable();
        QCOMPARE(state(), gw::CheckState::NotDone);
    }

    void notOfferedWithSecureBootOn()
    {
        offerable();
        write(QStringLiteral("sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c"),
              QByteArray("\x06\x00\x00\x00\x01", 5));
        QCOMPARE(state(), gw::CheckState::NotNeeded);
    }

    void notOfferedWhereTheKernelCannotHibernate()
    {
        offerable();
        write(QStringLiteral("sys/power/state"), "freeze mem\n");
        QCOMPARE(state(), gw::CheckState::NotNeeded);
    }

    // The item never sets resume=, so without one there is nothing to offer.
    void notOfferedWithoutAResumeDevice()
    {
        offerable();
        write(QStringLiteral("sys/power/resume"), "0:0\n");
        QCOMPARE(state(), gw::CheckState::NotNeeded);
    }

    // The resume device must be an active swap: here it names another one.
    void notOfferedWhenTheResumeDeviceIsNotTheSwap()
    {
        offerable();
        write(QStringLiteral("sys/power/resume"), "8:2\n");
        QCOMPARE(state(), gw::CheckState::NotNeeded);
    }

    // A swap file's line names no block device, so it is never offered.
    void notOfferedForASwapFile()
    {
        offerable();
        write(QStringLiteral("proc/swaps"), "Filename\tType\tSize\tUsed\tPriority\n"
                                            "/swap/swapfile file\t33554428\t0\t-2\n");
        QCOMPARE(state(), gw::CheckState::NotNeeded);
    }

    void notOfferedWhenTheSwapIsSmallerThanMemory()
    {
        offerable();
        write(QStringLiteral("proc/meminfo"), "MemTotal:       32781473 kB\n");
        QCOMPARE(state(), gw::CheckState::NotNeeded);
    }

    // A drop-in sorted later wins, and systemd's other spellings of yes and
    // no count.
    void doneOnlyWhenTheLastSettingsAreTheItemsOwn()
    {
        offerable();
        write(QStringLiteral("usr/lib/systemd/sleep.conf"), "[Sleep]\n#AllowSuspend=yes\n");
        write(QStringLiteral("etc/systemd/sleep.conf.d/50-mine.conf"),
              "[Sleep]\nAllowSuspend=false\nAllowHibernation=on\nAllowSuspendThenHibernate=0\n"
              "AllowHybridSleep=no\nHibernateMode=shutdown\n");
        QCOMPARE(state(), gw::CheckState::Done);
        write(QStringLiteral("etc/systemd/sleep.conf.d/60-later.conf"), "[Sleep]\nAllowSuspend=yes\n");
        QCOMPARE(state(), gw::CheckState::NotDone);
    }

    // The apply's file, read back by the check, is "done".
    void applyingWritesOneFileTheCheckReadsAsDone()
    {
        offerable();
        const QList<gw::Step> steps = gw::HibernateItem().applySteps({}, context());
        QCOMPARE(steps.size(), 2);
        QCOMPARE(steps[0].argv,
                 (QStringList{QStringLiteral("mkdir"), QStringLiteral("-p"), QStringLiteral("/etc/systemd/sleep.conf.d")}));
        QCOMPARE(steps[1].argv.value(1), QStringLiteral("of=/etc/systemd/sleep.conf.d/99-groundwork-hibernate.conf"));
        for (const gw::Step &s : steps)
            QVERIFY(s.needsRoot);
        write(QStringLiteral("etc/systemd/sleep.conf.d/99-groundwork-hibernate.conf"), steps[1].input);
        QCOMPARE(state(), gw::CheckState::Done);
    }

    void listedInTheCatalogueInOrder()
    {
        QVERIFY(gw::catalogue().find(QStringLiteral("hibernate-not-sleep")));
        QVERIFY(gw::catalogue().orderProblems().isEmpty());
    }
};

QTEST_MAIN(TstHibernateItem)
#include "tst_hibernateitem.moc"
