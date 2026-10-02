// The Wizard on Qt's offscreen display, with fake items and a fake Worker
// script: default toggles, dependency toggling with reasons, Apply's
// availability, the run page reading markers, and closing mid-run.
#include "gui/itemrow.h"
#include "gui/runpage.h"
#include "gui/wizard.h"

#include <QCheckBox>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

class FakeItem : public gw::Item
{
public:
    FakeItem(QString id, gw::Level level, bool installs, bool preparation = false)
        : m_id(std::move(id)), m_level(level), m_installs(installs), m_prep(preparation) {}
    QString id() const override { return m_id; }
    gw::Level level() const override { return m_level; }
    QString title() const override { return QStringLiteral("Title of ") + m_id; }
    QString applySentence() const override { return QStringLiteral("Does ") + m_id + QLatin1Char('.'); }
    bool installsPackages() const override { return m_installs; }
    bool isPreparation() const override { return m_prep; }
    gw::CheckResult check(const gw::CheckContext &) const override { return {}; }
    QList<gw::Step> applySteps(const gw::SystemIdentity &, const gw::CheckContext &) const override { return {}; }

private:
    QString m_id;
    gw::Level m_level;
    bool m_installs, m_prep;
};

const FakeItem update(QStringLiteral("update"), gw::Level::Essentials, false, true);
const FakeItem codecs(QStringLiteral("codecs"), gw::Level::Essentials, true);
const FakeItem hostname(QStringLiteral("hostname"), gw::Level::Configuration, false);
const FakeItem fonts(QStringLiteral("fonts"), gw::Level::NiceToHave, true);
const gw::Catalogue catalogue({&update, &codecs, &hostname, &fonts});

const QByteArray kFakeWorker = R"(#!/bin/sh
shift 3
n=0
for id in "$@"; do
  n=$((n+1))
  echo "@@STEP_BEGIN@@|$id|$n|$#|$id"
  if [ -n "$FAKE_STOP" ]; then
    while [ ! -f "$FAKE_STOP" ]; do sleep 0.05; done
    echo "@@STEP_END@@|$id|ok|"; echo "@@DONE@@|1|0|1"; exit 5
  fi
  echo "@@ACTION@@|$id|Doing $id"
  echo "plain output line"
  echo "@@STEP_END@@|$id|ok|"
done
echo "@@DONE@@|$#|0|0"
)";

} // namespace

class TstWizard : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    std::unique_ptr<gw::Wizard> make()
    {
        const QString worker = m_dir.filePath(QStringLiteral("fake-worker"));
        if (!QFile::exists(worker)) {
            QFile f(worker);
            if (f.open(QIODevice::WriteOnly))
                f.write(kFakeWorker);
            f.close();
            f.setPermissions(f.permissions() | QFile::ExeOwner);
        }
        gw::WizardSetup setup;
        setup.catalogue = &catalogue;
        setup.runChecks = [] {
            gw::CheckResults r;
            r.insert(QStringLiteral("update"), {gw::CheckState::NotDone, QStringLiteral("2 waiting")});
            r.insert(QStringLiteral("codecs"), {gw::CheckState::NotDone, {}});
            r.insert(QStringLiteral("hostname"), {gw::CheckState::NotDone, {}});
            r.insert(QStringLiteral("fonts"), {gw::CheckState::NotDone, {}});
            return r;
        };
        setup.language = QStringLiteral("en");
        setup.workerProgram = worker;
        setup.stateDir = m_dir.filePath(QStringLiteral("state"));
        auto w = std::make_unique<gw::Wizard>(setup);
        w->show();
        return w;
    }
    static void goToRunPage(gw::Wizard &w)
    {
        for (int i = 0; i < 10 && w.currentPage() != w.runPage(); ++i)
            w.next();
    }

private slots:
    void defaultsFollowTheSelectionRule()
    {
        auto w = make();
        QTRY_VERIFY(w->checksDone());
        QVERIFY(w->row(QStringLiteral("codecs"))->isChecked());
        QVERIFY(w->row(QStringLiteral("update"))->isChecked()); // pulled by codecs
        QVERIFY(!w->row(QStringLiteral("hostname"))->isChecked());
        QVERIFY(!w->row(QStringLiteral("fonts"))->isChecked());
    }

    void switchingOffADependencySwitchesOffWhatNeedsIt()
    {
        auto w = make();
        QTRY_VERIFY(w->checksDone());
        w->row(QStringLiteral("update"))->toggle()->click();
        QVERIFY(!w->row(QStringLiteral("codecs"))->isChecked());
        QVERIFY(w->row(QStringLiteral("codecs"))->reason().contains(QLatin1String("Title of update")));
    }

    void switchingOnAnInstallPullsTheUpdateAndSaysWhy()
    {
        auto w = make();
        QTRY_VERIFY(w->checksDone());
        w->row(QStringLiteral("update"))->toggle()->click(); // everything off
        w->row(QStringLiteral("fonts"))->toggle()->click();
        QVERIFY(w->row(QStringLiteral("update"))->isChecked());
        QVERIFY(w->row(QStringLiteral("update"))->reason().contains(QLatin1String("Title of fonts")));
    }

    void applyIsUnavailableWithNothingSwitchedOn()
    {
        auto w = make();
        QTRY_VERIFY(w->checksDone());
        w->row(QStringLiteral("update"))->toggle()->click(); // everything off
        for (int i = 0; i < 10 && !w->currentPage()->isCommitPage(); ++i)
            w->next();
        QVERIFY(w->currentPage()->isCommitPage());
        QVERIFY(!w->currentPage()->isComplete());
    }

    void runPageShowsTheWorkersProgress()
    {
        auto w = make();
        QTRY_VERIFY(w->checksDone());
        QCOMPARE(w->workerArguments(),
                 QStringList({"--worker", "--lang", "en", "update", "codecs"}));
        goToRunPage(*w);
        QTRY_VERIFY(w->runPage()->isComplete());
        QVERIFY(w->runPage()->statusOf(QStringLiteral("codecs")).contains(QLatin1String("done")));
        QCOMPARE(w->runPage()->summary(), QStringLiteral("All done."));
    }

    void closingMidRunAsksTheWorkerToStopAndWaits()
    {
        const QString stop = m_dir.filePath(QStringLiteral("state/stop.request"));
        QFile::remove(stop);
        qputenv("FAKE_STOP", stop.toUtf8());
        auto w = make();
        QTRY_VERIFY(w->checksDone());
        goToRunPage(*w);
        QTRY_VERIFY(w->runPage()->isRunning());
        w->close();
        QVERIFY(QFile::exists(stop));
        QVERIFY(!w->isVisible());
        QTRY_VERIFY(!w->runPage()->isRunning()); // the Worker finished its step and stopped
        qunsetenv("FAKE_STOP");
    }
};

QTEST_MAIN(TstWizard)
#include "tst_wizard.moc"
