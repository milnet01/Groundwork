// CommandRunner runs fakes placed first on PATH, so no test runs a real
// system tool.
#include "core/commandrunner.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TstCommandRunner : public QObject
{
    Q_OBJECT

    QTemporaryDir m_bin;
    QByteArray m_oldPath;

    void fake(const QString &name, const QByteArray &body)
    {
        QFile f(m_bin.filePath(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\n" + body + "\n");
        f.close();
        QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_bin.isValid());
        m_oldPath = qgetenv("PATH");
        qputenv("PATH", m_bin.path().toUtf8() + ':' + m_oldPath);
        fake(QStringLiteral("gw-echo"), "printf '%s|%s' \"$1\" \"$LC_ALL\"; echo oops >&2");
        fake(QStringLiteral("gw-exit"), "exit \"$1\"");
        fake(QStringLiteral("gw-sleep"), "exec sleep 5");
    }
    void cleanupTestCase() { qputenv("PATH", m_oldPath); }

    void capturesOutputAndFixesLocale()
    {
        const auto r = gw::CommandRunner().run({QStringLiteral("gw-echo"), QStringLiteral("hi")});
        QVERIFY(r.ok());
        QCOMPARE(r.out, QByteArray("hi|C"));
        QCOMPARE(r.err, QByteArray("oops\n"));
    }

    void argumentsAreNotInterpretedByAShell()
    {
        const auto r = gw::CommandRunner().run(
            {QStringLiteral("gw-echo"), QStringLiteral("$(echo injected)")});
        QCOMPARE(r.out, QByteArray("$(echo injected)|C"));
    }

    void reportsExitCode()
    {
        const auto r = gw::CommandRunner().run({QStringLiteral("gw-exit"), QStringLiteral("106")});
        QVERIFY(r.started);
        QCOMPARE(r.exitCode, 106);
        QVERIFY(!r.ok());
    }

    void missingProgramDidNotStart()
    {
        const auto r = gw::CommandRunner().run({QStringLiteral("gw-no-such-program")});
        QVERIFY(!r.started);
        QVERIFY(!r.ok());
    }

    void timesOut()
    {
        QElapsedTimer t;
        t.start();
        const auto r = gw::CommandRunner().run({QStringLiteral("gw-sleep")}, 300);
        QVERIFY(r.timedOut);
        QVERIFY(!r.ok());
        QVERIFY(t.elapsed() < 4000);
    }
};

QTEST_GUILESS_MAIN(TstCommandRunner)
#include "tst_commandrunner.moc"
