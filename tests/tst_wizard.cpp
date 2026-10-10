// The Wizard on Qt's offscreen display, with fake items and a fake Worker
// script: default toggles, dependency toggling with reasons, Apply's
// availability, the run page reading markers, and closing mid-run.
#include "core/translations.h"
#include "gui/itemrow.h"
#include "gui/runpage.h"
#include "gui/wizard.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardItemModel>
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
class FakeValueItem : public FakeItem
{
public:
    using FakeItem::FakeItem;
    QString valuePrompt() const override { return QStringLiteral("Name:"); }
    bool isValidValue(const QString &v) const override { return !v.isEmpty() && !v.contains(QLatin1Char(' ')); }
};
const FakeValueItem hostname(QStringLiteral("hostname"), gw::Level::Configuration, false);
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

    std::unique_ptr<gw::Wizard> make(const QString &language = QStringLiteral("en"))
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
        setup.language = language;
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
    void cleanup() { gw::loadLanguage(QStringLiteral("en")); }

    // Qt has no words of its own for Afrikaans, so the Wizard labels its
    // own buttons (GRND-0033). None may keep Qt's English.
    void itsButtonsSpeakTheChosenLanguage()
    {
        QVERIFY(gw::loadLanguage(QStringLiteral("af")));
        auto w = make();
        const QList<std::pair<QWizard::WizardButton, QString>> qtEnglish{
            {QWizard::BackButton, QStringLiteral("< &Back")},
            {QWizard::NextButton, QStringLiteral("&Next >")},
            {QWizard::FinishButton, QStringLiteral("&Finish")},
            {QWizard::CancelButton, QStringLiteral("Cancel")}};
        for (const auto &[which, english] : qtEnglish)
            QVERIFY2(w->buttonText(which) != english, qPrintable(english));
    }

    // A translation no native speaker has checked is marked as a draft in
    // the language choice; English carries no mark (GRND-0041; docs/
    // design.md, Text). Afrikaans awaits its check.
    void theLanguageChoiceMarksDrafts()
    {
        auto w = make();
        auto *languages = w->page(0)->findChild<QComboBox *>();
        QVERIFY(languages);
        const int en = languages->findData(QStringLiteral("en"));
        const int af = languages->findData(QStringLiteral("af"));
        QVERIFY(en >= 0 && af >= 0);
        QCOMPARE(languages->itemText(en), QStringLiteral("English"));
        const QString name = QLocale(QStringLiteral("af")).nativeLanguageName();
        QVERIFY2(languages->itemText(af).contains(name) && languages->itemText(af) != name,
                 qPrintable(languages->itemText(af)));
    }

    // A language whose script no installed font covers is named in English
    // and cannot be chosen, so it never shows as empty boxes (GRND-0035).
    void aLanguageWithoutItsFontCannotBeChosen()
    {
        QVERIFY(gw::fontShowsLanguage(QStringLiteral("en")));
        QVERIFY(gw::fontShowsLanguage(QStringLiteral("af")));
        auto w = make();
        auto *languages = w->page(0)->findChild<QComboBox *>();
        auto *model = qobject_cast<QStandardItemModel *>(languages->model());
        QVERIFY(model);
        for (int i = 0; i < languages->count(); ++i) {
            const QString code = languages->itemData(i).toString();
            const bool shows = gw::fontShowsLanguage(code);
            QVERIFY2(model->item(i)->isEnabled() == shows, qPrintable(code));
            if (!shows)
                QVERIFY2(!languages->itemText(i).contains(QLocale(code).nativeLanguageName()), qPrintable(code));
        }
    }

    // In a draft language, the first page says so; in English it says nothing.
    void theFirstPageSaysWhenItsLanguageIsADraft()
    {
        {
            auto w = make();
            auto *notice = w->page(0)->findChild<QLabel *>(QStringLiteral("draftNotice"));
            QVERIFY(!notice || notice->isHidden());
        }
        QVERIFY(gw::loadLanguage(QStringLiteral("af")));
        auto w = make(QStringLiteral("af"));
        auto *notice = w->page(0)->findChild<QLabel *>(QStringLiteral("draftNotice"));
        QVERIFY(notice);
        QVERIFY(!notice->isHidden());
        QVERIFY(!notice->text().isEmpty());
    }

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

    // A note stays only while what it says is true (GRND-0047): switching
    // the needed item back on, or the row itself, clears it.
    void aSwitchedOffNoteGoesWhenItNoLongerHolds()
    {
        auto w = make();
        QTRY_VERIFY(w->checksDone());
        auto *codecsRow = w->row(QStringLiteral("codecs"));
        w->row(QStringLiteral("update"))->toggle()->click();
        QVERIFY(!codecsRow->reason().isEmpty());
        w->row(QStringLiteral("update"))->toggle()->click();
        QVERIFY(!codecsRow->isChecked()); // design: switching on pulls only what it needs
        QVERIFY2(codecsRow->reason().isEmpty(), qPrintable(codecsRow->reason()));
    }

    void aSwitchedOffNoteGoesWhenItsRowIsSwitchedBackOn()
    {
        auto w = make();
        QTRY_VERIFY(w->checksDone());
        auto *codecsRow = w->row(QStringLiteral("codecs"));
        w->row(QStringLiteral("update"))->toggle()->click();
        QVERIFY(!codecsRow->reason().isEmpty());
        codecsRow->toggle()->click(); // pulls the update back on
        QVERIFY(codecsRow->isChecked());
        QVERIFY2(codecsRow->reason().isEmpty(), qPrintable(codecsRow->reason()));
    }

    void aSwitchedOnNoteGoesWhenItNoLongerHolds()
    {
        auto w = make();
        QTRY_VERIFY(w->checksDone());
        auto *updateRow = w->row(QStringLiteral("update"));
        updateRow->toggle()->click(); // everything off
        w->row(QStringLiteral("fonts"))->toggle()->click();
        QVERIFY(!updateRow->reason().isEmpty());
        w->row(QStringLiteral("fonts"))->toggle()->click();
        QVERIFY2(updateRow->reason().isEmpty(), qPrintable(updateRow->reason()));
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

    void offersOneUpOnlyWhenItIsInstalled()
    {
        // A fake oneup on PATH that leaves a mark when started.
        const QString bin = m_dir.filePath(QStringLiteral("oneup-bin"));
        QDir().mkpath(bin);
        const QString mark = m_dir.filePath(QStringLiteral("oneup-started"));
        QFile f(bin + QStringLiteral("/oneup"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\ntouch '" + mark.toUtf8() + "'\n");
        f.close();
        QVERIFY(f.setPermissions(f.permissions() | QFile::ExeOwner));
        const QByteArray oldPath = qgetenv("PATH");

        qputenv("PATH", "/usr/bin:/bin"); // no oneup
        {
            auto w = make();
            QTRY_VERIFY(w->checksDone());
            goToRunPage(*w);
            QTRY_VERIFY(w->runPage()->isComplete());
            QVERIFY(w->runPage()->oneUpButton()->isHidden());
        }
        qputenv("PATH", bin.toUtf8() + ":/usr/bin:/bin");
        {
            auto w = make();
            QTRY_VERIFY(w->checksDone());
            goToRunPage(*w);
            QTRY_VERIFY(w->runPage()->isComplete());
            QVERIFY(!w->runPage()->oneUpButton()->isHidden());
            w->runPage()->oneUpButton()->click();
            QTRY_VERIFY(QFile::exists(mark));
        }
        qputenv("PATH", oldPath);
    }

    void aValueItemNeedsAValidValueAndPassesIt()
    {
        auto w = make();
        QTRY_VERIFY(w->checksDone());
        gw::ItemRow *row = w->row(QStringLiteral("hostname"));
        QVERIFY(row->valueField());
        row->toggle()->click();
        row->valueField()->setText(QStringLiteral("bad name"));
        QVERIFY(!w->valuesValid());
        for (int i = 0; i < 10 && !w->currentPage()->isCommitPage(); ++i)
            w->next();
        QVERIFY(!w->currentPage()->isComplete()); // Apply unavailable
        row->valueField()->setText(QStringLiteral("lounge-pc"));
        QVERIFY(w->valuesValid());
        QVERIFY(w->currentPage()->isComplete());
        const QStringList args = w->workerArguments();
        QVERIFY(args.join(QLatin1Char(' ')).contains(QLatin1String("--set hostname=lounge-pc")));
        QVERIFY(w->row(QStringLiteral("codecs"))->valueField() == nullptr);
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
