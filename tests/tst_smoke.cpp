// Proves the test toolchain itself: Qt Test builds, runs under CTest, and a
// failing check fails the run. Real tests arrive with each part.
#include <QtTest>

class TstSmoke : public QObject
{
    Q_OBJECT
private slots:
    void qtTestRuns() { QCOMPARE(1 + 1, 2); }
};

QTEST_GUILESS_MAIN(TstSmoke)
#include "tst_smoke.moc"
