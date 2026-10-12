// Virtual machines (GRND-0064), against a fake rpm first on PATH that
// knows the packages listed in FAKE_INSTALLED, and a fixture root holding
// /proc/cpuinfo.
#include "items/virtualmachinesitem.h"

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

// Two processors, as the kernel lists them; flags is the line that counts.
QByteArray cpuinfo(const QByteArray &flags)
{
    const QByteArray cpu = "processor\t: 0\nmodel name\t: Some CPU\nflags\t\t: " + flags + "\n\n";
    return cpu + cpu;
}
} // namespace

class TstVirtualMachinesItem : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin;
    QByteArray m_oldPath;
    std::unique_ptr<QTemporaryDir> m_root;

    void installed(const QByteArray &packages) { qputenv("FAKE_INSTALLED", " " + packages + " "); }
    void cpu(const QByteArray &flags) { write(m_root->filePath(QStringLiteral("proc/cpuinfo")), cpuinfo(flags)); }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }
    gw::CheckState state() const { return gw::VirtualMachinesItem().check(context()).state; }

private slots:
    void initTestCase()
    {
        write(m_bin.filePath(QStringLiteral("rpm")),
              "#!/bin/sh\ncase \"$FAKE_INSTALLED\" in *\" $2 \"*) exit 0;; esac\nexit 1\n", true);
        m_oldPath = qgetenv("PATH");
        qputenv("PATH", m_bin.path().toUtf8() + ":/usr/bin:/bin");
    }
    void cleanupTestCase() { qputenv("PATH", m_oldPath); }
    void init()
    {
        m_root = std::make_unique<QTemporaryDir>();
        installed("");
    }

    // vmx is Intel's flag, svm AMD's; flags that only contain the letters
    // do not count.
    void notNeededWithoutVirtualisation()
    {
        cpu("fpu vme de pse svm_lock vmxe");
        QCOMPARE(state(), gw::CheckState::NotNeeded);
    }

    void couldNotTellWithoutCpuinfo() { QCOMPARE(state(), gw::CheckState::CouldNotTell); }

    void doneWhenTheManagerAndQemuDriverAreInstalled_data()
    {
        QTest::addColumn<QByteArray>("flags");
        QTest::newRow("Intel") << QByteArray("fpu vme vmx sse2");
        QTest::newRow("AMD") << QByteArray("fpu svm sse2");
    }
    void doneWhenTheManagerAndQemuDriverAreInstalled()
    {
        QFETCH(QByteArray, flags);
        cpu(flags);
        QCOMPARE(state(), gw::CheckState::NotDone);
        installed("virt-manager");
        QCOMPARE(state(), gw::CheckState::NotDone);
        installed("libvirt-daemon-qemu");
        QCOMPARE(state(), gw::CheckState::NotDone);
        installed("virt-manager libvirt-daemon-qemu");
        QCOMPARE(state(), gw::CheckState::Done);
    }

    void applyingInstallsOpenSusesKvmPattern()
    {
        cpu("vmx");
        const QList<gw::Step> steps = gw::VirtualMachinesItem().applySteps({}, context());
        QCOMPARE(steps.size(), 1);
        QVERIFY(steps[0].needsRoot);
        QCOMPARE(steps[0].tool, gw::Step::Tool::Zypper);
        QCOMPARE(steps[0].argv, (QStringList{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"),
                                             QStringLiteral("-t"), QStringLiteral("pattern"),
                                             QStringLiteral("kvm_tools")}));
    }
};

QTEST_MAIN(TstVirtualMachinesItem)
#include "tst_virtualmachinesitem.moc"
