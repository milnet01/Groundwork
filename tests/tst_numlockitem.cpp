// NumLock on at login (GRND-0060), against a fixture root holding the
// login-screen choice and SDDM's configuration files.
#include "items/numlockitem.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstNumlockItem : public QObject
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
    void link(const QString &path, const QString &target)
    {
        const QString full = m_root->filePath(path);
        QVERIFY(QDir().mkpath(QFileInfo(full).path()));
        QFile::remove(full);
        QVERIFY(QFile::link(target, full));
    }
    void numlock(const QString &path, const QByteArray &value)
    {
        write(path, "[Theme]\nNumlock=off\n[General]\nNumlock=" + value + "\n[Users]\nNumlock=off\n");
    }
    // openSUSE chooses the login screen with an alternative.
    void sddmChosen() { link(QStringLiteral("etc/alternatives/default-displaymanager"),
                             QStringLiteral("/usr/lib/X11/displaymanagers/sddm")); }
    gw::CheckContext context() const { return gw::CheckContext(gw::FileReader(m_root->path())); }
    gw::CheckState state() const { return gw::NumlockItem().check(context()).state; }

private slots:
    void init() { m_root = std::make_unique<QTemporaryDir>(); }

    void notNeededWhereSddmIsNotTheLoginScreen()
    {
        QCOMPARE(state(), gw::CheckState::NotNeeded);
        link(QStringLiteral("etc/alternatives/default-displaymanager"),
             QStringLiteral("/usr/lib/X11/displaymanagers/gdm"));
        QCOMPARE(state(), gw::CheckState::NotNeeded);
    }

    void sddmAsTheLoginServiceCounts()
    {
        link(QStringLiteral("etc/systemd/system/display-manager.service"),
             QStringLiteral("/usr/lib/systemd/system/sddm.service"));
        QCOMPARE(state(), gw::CheckState::NotDone);
    }

    // SDDM reads its system folder, then the local one, then sddm.conf; the
    // last [General] Numlock wins. Its default, none, leaves NumLock alone.
    void theSettingSddmUsesDecides()
    {
        sddmChosen();
        QCOMPARE(state(), gw::CheckState::NotDone);
        numlock(QStringLiteral("usr/lib/sddm/sddm.conf.d/50-x.conf"), "on");
        QCOMPARE(state(), gw::CheckState::Done);
        numlock(QStringLiteral("etc/sddm.conf.d/10-mine.conf"), "off");
        QCOMPARE(state(), gw::CheckState::NotDone);
        numlock(QStringLiteral("etc/sddm.conf.d/20-later.conf"), "on");
        QCOMPARE(state(), gw::CheckState::Done);
        numlock(QStringLiteral("etc/sddm.conf"), "none");
        QCOMPARE(state(), gw::CheckState::NotDone);
    }

    void applyingWritesTheSetting()
    {
        sddmChosen();
        const QList<gw::Step> steps = gw::NumlockItem().applySteps({}, context());
        QCOMPARE(steps.size(), 2);
        QCOMPARE(steps[0].argv, (QStringList{QStringLiteral("mkdir"), QStringLiteral("-p"),
                                             QStringLiteral("/etc/sddm.conf.d")}));
        QCOMPARE(steps[1].argv.value(1), QStringLiteral("of=/etc/sddm.conf.d/99-groundwork-numlock.conf"));
        for (const gw::Step &s : steps)
            QVERIFY(s.needsRoot);

        // What it writes is what the check reads as done.
        write(QStringLiteral("etc/sddm.conf.d/99-groundwork-numlock.conf"), steps[1].input);
        QCOMPARE(state(), gw::CheckState::Done);
    }
};

QTEST_MAIN(TstNumlockItem)
#include "tst_numlockitem.moc"
