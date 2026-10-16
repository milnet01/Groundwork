// The Wizard on Qt's offscreen display, with fake items and a fake Worker
// script: default toggles, dependency toggling with reasons, Apply's
// availability, the run page reading markers, and closing mid-run.
#include "core/translations.h"
#include "gui/itemrow.h"
#include "gui/runpage.h"
#include "gui/themes.h"
#include "gui/wizard.h"

#include <QAccessible>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
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

constexpr qint64 kGiB = gw::kGiB;

// A UEFI computer with 8 GiB of memory and three drives: a blank USB stick,
// the system's own drive, and a spinning disk with Windows and free space.
gw::DriveSurvey fakeSurvey()
{
    gw::Drive stick;
    stick.path = QStringLiteral("/dev/sdb");
    stick.model = QStringLiteral("Blank Stick");
    stick.serial = QStringLiteral("B1");
    stick.tran = QStringLiteral("usb");
    stick.size = 100 * kGiB;
    gw::Drive system;
    system.path = QStringLiteral("/dev/nvme0n1");
    system.model = QStringLiteral("System Drive");
    system.tran = QStringLiteral("nvme");
    system.size = 500 * kGiB;
    system.inUse = QStringLiteral("In use: /dev/nvme0n1p2 is mounted at /.");
    gw::Drive windows;
    windows.path = QStringLiteral("/dev/sdc");
    windows.model = QStringLiteral("Windows Disk");
    windows.pttype = QStringLiteral("gpt");
    windows.ptuuid = QStringLiteral("0b6f8a37-1c2d-4e5f-9a0b-1c2d3e4f5a6b");
    windows.size = 500 * kGiB;
    windows.rotational = true;
    windows.partitions = {{QStringLiteral("/dev/sdc1"), gw::kMiB, 100 * gw::kMiB, QStringLiteral("vfat"), {}, {}, {}},
                          {QStringLiteral("/dev/sdc2"), 101 * gw::kMiB, 200 * kGiB, QStringLiteral("ntfs"),
                           QStringLiteral("Windows"), {}, {}}};
    return {{stick, system, windows}, gw::Firmware::Uefi, 8 * kGiB};
}

} // namespace

class TstWizard : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    std::unique_ptr<gw::Wizard> make(const QString &language = QStringLiteral("en"),
                                     const gw::Catalogue *items = &catalogue,
                                     QList<gw::Job> jobs = {gw::Job::SetUp}, bool live = false)
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
        setup.catalogue = items;
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
        setup.jobs = jobs;
        setup.liveSystem = live;
        setup.surveyDrives = fakeSurvey;
        auto w = std::make_unique<gw::Wizard>(setup);
        w->show();
        return w;
    }
    // The prepare job, from the first page to the page after choosing drive.
    std::unique_ptr<gw::Wizard> pickDrive(const QString &path)
    {
        auto w = make(QStringLiteral("en"), &catalogue, {gw::Job::SetUp, gw::Job::PrepareDrive});
        w->chooseJob(gw::Job::PrepareDrive);
        w->next();
        auto *pick = w->currentPage()->findChild<QRadioButton *>(path);
        if (!pick)
            return nullptr;
        pick->click();
        w->next();
        return w;
    }
    static QLabel *shownLabel(QWidget *page, const QString &name)
    {
        auto *label = page->findChild<QLabel *>(name);
        return label && !label->isHidden() ? label : nullptr;
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

    // The update is a preparation, always "not done"; "not done — No
    // updates were waiting" read as a contradiction (GRND-0051). Its row
    // says what it found, with no state word.
    void aPreparationRowSaysWhatItFoundWithoutAStateWord()
    {
        auto w = make();
        QTRY_VERIFY(w->checksDone());
        QStringList texts;
        for (auto *label : w->row(QStringLiteral("update"))->findChildren<QLabel *>())
            texts << label->text();
        QVERIFY2(texts.contains(QStringLiteral("2 waiting")), qPrintable(texts.join(QLatin1Char('|'))));
        for (auto *label : w->row(QStringLiteral("codecs"))->findChildren<QLabel *>())
            texts << label->text();
        QVERIFY2(texts.contains(QStringLiteral("not done")), qPrintable(texts.join(QLatin1Char('|'))));
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

        // No oneup: not /usr/bin, where OneUp installs it (GRND-0080).
        const QString empty = m_dir.filePath(QStringLiteral("empty-bin"));
        QDir().mkpath(empty);
        qputenv("PATH", empty.toUtf8());
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

    // Each row's choice is an on/off switch, not a tick box (GRND-0069):
    // the track fills with the theme's accent when on and empty when off,
    // grows with the font, toggles from the keyboard, and tells a screen
    // reader it is a checkable control named for the item.
    void eachRowIsAnOnOffSwitch()
    {
        gw::ItemRow row(codecs, {gw::CheckState::NotDone, {}});
        QCheckBox *toggle = row.toggle();
        QPalette palette = toggle->palette();
        const QColor accent(0x12, 0xc4, 0x5a);
        palette.setColor(QPalette::Highlight, accent);
        toggle->setPalette(palette);
        row.show();
        QVERIFY(QTest::qWaitForWindowExposed(&row));

        const auto accentPixels = [&] {
            const QImage image = toggle->grab().toImage();
            int count = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    count += QColor(image.pixel(x, y)) == accent;
            return count;
        };
        row.setChecked(false);
        QCOMPARE(accentPixels(), 0);
        row.setChecked(true);
        const int side = toggle->fontMetrics().height();
        const int onAtNormal = accentPixels();
        // A track wider than it is tall; a tick box of the same height
        // would hold at most side * side.
        QVERIFY2(onAtNormal > side * side, qPrintable(QStringLiteral("%1 <= %2").arg(onAtNormal).arg(side * side)));

        QFont large = row.font();
        large.setPointSizeF(large.pointSizeF() * 2);
        row.setFont(large);
        QVERIFY2(accentPixels() > 2 * onAtNormal, qPrintable(QString::number(accentPixels())));

        toggle->setFocus();
        QTest::keyClick(toggle, Qt::Key_Space);
        QVERIFY(!row.isChecked());

        QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(toggle);
        QVERIFY(accessible);
        QCOMPARE(accessible->role(), QAccessible::CheckBox);
        QVERIFY(accessible->state().checkable);
        QCOMPARE(accessible->text(QAccessible::Name), codecs.title());
    }

    // The release check's Essentials page showed two of its six rows until
    // the window was maximised (GRND-0049). It opens showing them all.
    void aLevelsRowsShowWithoutScrolling()
    {
        QList<FakeItem> six;
        for (int i = 0; i < 6; ++i)
            six.append(FakeItem(QStringLiteral("item%1").arg(i), gw::Level::Essentials, false));
        QList<const gw::Item *> items;
        for (const FakeItem &item : six)
            items.append(&item);
        const gw::Catalogue sixRows(items);
        auto w = make(QStringLiteral("en"), &sixRows);
        QTRY_VERIFY(w->checksDone());
        w->next();
        auto *scroll = w->currentPage()->findChild<QScrollArea *>();
        QVERIFY(scroll);
        QTRY_COMPARE(scroll->verticalScrollBar()->maximum(), 0);
    }

    // The first page offers colours, following the desktop until one is
    // chosen; a choice paints the window and outlives a change of language
    // (GRND-0050).
    void theFirstPageOffersColoursThatOutliveALanguageChange()
    {
        auto w = make();
        auto *choice = w->page(0)->findChild<QComboBox *>(QStringLiteral("themes"));
        QVERIFY(choice);
        QCOMPARE(choice->currentData().toString(), QString::fromLatin1(gw::kFollowDesktop));
        choice->setCurrentIndex(choice->findData(QStringLiteral("high-contrast-dark")));
        QCOMPARE(QApplication::palette().color(QPalette::Window), QColor(Qt::black));
        QCOMPARE(qgetenv("GROUNDWORK_THEME"), QByteArray("high-contrast-dark"));

        auto rebuilt = make(QStringLiteral("af"));
        auto *again = rebuilt->page(0)->findChild<QComboBox *>(QStringLiteral("themes"));
        QCOMPARE(again->currentData().toString(), QStringLiteral("high-contrast-dark"));

        gw::applyTheme(QString::fromLatin1(gw::kFollowDesktop));
        qunsetenv("GROUNDWORK_THEME");
    }

    // GRND-0077: one job built, so the first page shows no choice.
    void oneJobShowsNoChoice()
    {
        auto w = make();
        QVERIFY(!w->currentPage()->findChild<QWidget *>(QStringLiteral("jobs")));
        QCOMPARE(w->chosenJob(), gw::Job::SetUp);
        QTRY_VERIFY(w->checksDone());
        QVERIFY(w->button(QWizard::NextButton)->isEnabled());
    }

    // GRND-0077: with more than one job built, the first page offers each,
    // the first chosen.
    void severalJobsAreOfferedAsAChoice()
    {
        auto w = make(QStringLiteral("en"), &catalogue,
                      {gw::Job::SetUp, gw::Job::CarryOver, gw::Job::PrepareDrive});
        auto *jobs = w->currentPage()->findChild<QWidget *>(QStringLiteral("jobs"));
        QVERIFY(jobs);
        const auto choices = jobs->findChildren<QRadioButton *>();
        QCOMPARE(choices.size(), 3);
        QVERIFY(choices[0]->isChecked());
        QCOMPARE(w->chosenJob(), gw::Job::SetUp);
        choices[2]->setChecked(true);
        QCOMPARE(w->chosenJob(), gw::Job::PrepareDrive);
    }

    // GRND-0077: a live system keeps nothing, so it is offered only
    // preparing a drive; its checks never run.
    void aLiveSystemIsOfferedOnlyPreparingADrive()
    {
        auto w = make(QStringLiteral("en"), &catalogue,
                      {gw::Job::SetUp, gw::Job::CarryOver, gw::Job::PrepareDrive}, true);
        QCOMPARE(w->offeredJobs(), QList<gw::Job>{gw::Job::PrepareDrive});
        QVERIFY(!w->currentPage()->findChild<QWidget *>(QStringLiteral("jobs")));
        QTest::qWait(200);
        QVERIFY(!w->checksDone());
    }

    // GRND-0077: until preparing a drive is built, a live system is offered
    // nothing, and the first page says why.
    void aLiveSystemWithNothingBuiltSaysWhy()
    {
        auto w = make(QStringLiteral("en"), &catalogue, {gw::Job::SetUp}, true);
        QVERIFY(w->offeredJobs().isEmpty());
        QVERIFY(!w->chosenJob());
        bool said = false;
        for (auto *label : w->currentPage()->findChildren<QLabel *>())
            said = said || label->text().contains(QStringLiteral("live system"));
        QVERIFY(said);
        QVERIFY(!w->button(QWizard::NextButton)->isEnabled());
    }

    // GRND-0076, INV-1 shown: a drive in use is listed, greyed, saying why.
    void aDriveInUseCannotBeChosen()
    {
        auto w = make(QStringLiteral("en"), &catalogue, {gw::Job::SetUp, gw::Job::PrepareDrive});
        w->chooseJob(gw::Job::PrepareDrive);
        w->next();
        auto *system = w->currentPage()->findChild<QRadioButton *>(QStringLiteral("/dev/nvme0n1"));
        QVERIFY(system);
        QVERIFY(!system->isEnabled());
        bool said = false;
        for (auto *label : w->currentPage()->findChildren<QLabel *>())
            said = said || label->text().contains(QLatin1String("mounted at /"));
        QVERIFY(said);
        QVERIFY(!w->currentPage()->isComplete()); // nothing chosen yet
        w->currentPage()->findChild<QRadioButton *>(QStringLiteral("/dev/sdb"))->click();
        QVERIFY(w->currentPage()->isComplete());
    }

    // GRND-0076, spec 4.3: Windows on the drive brings its checklist first,
    // and Continue needs no switch; a blank drive goes straight to layouts.
    void windowsOnTheDriveShowsTheChecklistFirst()
    {
        auto w = pickDrive(QStringLiteral("/dev/sdc"));
        QVERIFY(w);
        QCOMPARE(w->currentPage()->objectName(), QStringLiteral("windowsChecklist"));
        QVERIFY(w->currentPage()->isComplete());
        QVERIFY(w->currentPage()->findChildren<QCheckBox *>().isEmpty());
        w->next();
        QVERIFY(w->currentPage()->findChild<QWidget *>(QStringLiteral("hibernate")));
        // Beside Windows is offered, in the free space.
        QVERIFY(w->currentPage()->findChild<QRadioButton *>(
            QStringLiteral("suggestion%1").arg(int(gw::Suggestion::Beside))));

        auto blank = pickDrive(QStringLiteral("/dev/sdb"));
        QVERIFY(blank->currentPage()->findChild<QWidget *>(QStringLiteral("hibernate")));
    }

    // GRND-0076, INV-9 on the page: 8 GiB of swap is more than 5 % of a
    // 100 GiB stick, so the switch starts off and says its share; on a
    // 500 GiB disk it starts on.
    void roomToHibernateStartsByItsShare()
    {
        auto w = pickDrive(QStringLiteral("/dev/sdb"));
        auto *hibernate = w->currentPage()->findChild<QCheckBox *>(QStringLiteral("hibernate"));
        QVERIFY(hibernate);
        QVERIFY(!hibernate->isChecked());
        QLabel *note = shownLabel(w->currentPage(), QStringLiteral("hibernateNote"));
        QVERIFY(note);
        QVERIFY2(note->text().contains(QLatin1String("GiB")) && note->text().contains(QLatin1String("%")),
                 qPrintable(note->text()));

        auto big = pickDrive(QStringLiteral("/dev/sdc"));
        big->next(); // past Windows' checklist
        QVERIFY(big->currentPage()->findChild<QCheckBox *>(QStringLiteral("hibernate"))->isChecked());
        QVERIFY(!shownLabel(big->currentPage(), QStringLiteral("hibernateNote")));
    }

    // GRND-0076, spec 4.3: a custom layout goes on only when it is valid.
    void aCustomLayoutMustBeValid()
    {
        auto w = pickDrive(QStringLiteral("/dev/sdb"));
        w->currentPage()->findChild<QRadioButton *>(QStringLiteral("custom"))->click();
        auto *editor = w->currentPage()->findChild<gw::CustomLayoutEditor *>();
        QVERIFY(editor && !editor->isHidden());
        QVERIFY(w->currentPage()->isComplete()); // it starts as the whole-drive layout
        editor->removePartition(editor->count() - 1); // the system partition
        QVERIFY(!w->currentPage()->isComplete());
        QVERIFY(shownLabel(w->currentPage(), QStringLiteral("layoutProblem")));
        editor->addPartition({gw::Use::System, std::nullopt});
        QVERIFY(w->currentPage()->isComplete());
        QVERIFY(!shownLabel(w->currentPage(), QStringLiteral("layoutProblem")));
    }

    // GRND-0076, INV-2: Apply on the confirm page is unavailable until the
    // "I understand" switch is on. Then the layout file is written and the
    // Worker started with it.
    void applyWaitsForTheIUnderstandSwitch()
    {
        auto w = pickDrive(QStringLiteral("/dev/sdb"));
        w->next();
        QVERIFY(w->currentPage()->isCommitPage());
        auto *understand = w->currentPage()->findChild<QCheckBox *>(QStringLiteral("understand"));
        QVERIFY(understand);
        QVERIFY(!understand->isChecked());
        QVERIFY(!w->currentPage()->isComplete());
        QVERIFY(!w->button(QWizard::CommitButton)->isEnabled());
        understand->click();
        QVERIFY(w->currentPage()->isComplete());
        QVERIFY(w->button(QWizard::CommitButton)->isEnabled());
        understand->click();
        QVERIFY(!w->button(QWizard::CommitButton)->isEnabled());
        understand->click();

        const QString layoutPath = m_dir.filePath(QStringLiteral("state/layout.json"));
        QFile::remove(layoutPath);
        w->button(QWizard::CommitButton)->click();
        QCOMPARE(w->currentPage(), w->prepareRunPage());
        QFile file(layoutPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QString why;
        const auto read = gw::readLayoutFile(file.readAll(), &why);
        QVERIFY2(read, qPrintable(why));
        QCOMPARE(read->drive.path, QStringLiteral("/dev/sdb"));
        QCOMPARE(read->drive.serial, QStringLiteral("B1"));
        QCOMPARE(w->prepareArguments(),
                 QStringList({"--worker", "--prepare-drive", layoutPath, "--lang", "en"}));
        QTRY_VERIFY(w->prepareRunPage()->isComplete());
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
