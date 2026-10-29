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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
class FakeIncluder : public FakeItem
{
public:
    using FakeItem::FakeItem;
    QStringList includes() const override { return {QStringLiteral("parser")}; }
};
const FakeItem parser(QStringLiteral("parser"), gw::Level::NiceToHave, false);
const FakeIncluder terminal(QStringLiteral("terminal"), gw::Level::NiceToHave, false);
const gw::Catalogue including({&parser, &terminal});
// GRND-0082: an item another one offers too.
class FakeOffered : public FakeItem
{
public:
    using FakeItem::FakeItem;
    QStringList alsoOfferedBy() const override { return {QStringLiteral("host")}; }
};
const FakeOffered assistant(QStringLiteral("assistant"), gw::Level::NiceToHave, false);
const FakeItem host(QStringLiteral("host"), gw::Level::NiceToHave, false);
const gw::Catalogue offering({&assistant, &host});
// Starting from a saved setup (GRND-0079): a kept item that is not on by
// default, and one that accepts a licence.
class FakeLicenceItem : public FakeItem
{
public:
    using FakeItem::FakeItem;
    bool acceptsLicence() const override { return true; }
};
const FakeItem firewall(QStringLiteral("firewall"), gw::Level::SystemSetup, false);
const FakeLicenceItem nvidia(QStringLiteral("nvidia"), gw::Level::Essentials, true);
const gw::Catalogue restoring({&update, &nvidia, &firewall});

const QByteArray kFakeWorker = R"(#!/bin/sh
if [ -n "$FAKE_WORKER_LOG" ]; then echo "$*" >> "$FAKE_WORKER_LOG"; fi
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

// A UEFI computer with 8 GiB of memory and four drives: a blank USB stick,
// the system's own drive, a spinning disk with Windows and free space, and
// a Windows disk whose free space fits openSUSE only without hibernating.
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
    gw::Drive tight = windows;
    tight.path = QStringLiteral("/dev/sdd");
    tight.model = QStringLiteral("Tight Windows Disk");
    tight.ptuuid = QStringLiteral("5d2e9c41-7a3b-4c8d-9e0f-2a3b4c5d6e7f");
    tight.size = 229 * kGiB; // about 29 GiB free: 24 needed, 30 to hibernate
    for (gw::Partition &p : tight.partitions)
        p.path.replace(QLatin1String("sdc"), QLatin1String("sdd"));
    return {{stick, system, windows, tight}, gw::Firmware::Uefi, 8 * kGiB};
}

} // namespace

class TstWizard : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    qint64 m_free = 100 * kGiB; // the save folder's free space
    gw::Manifest m_saved;       // what the saved file holds

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
            r.insert(QStringLiteral("firewall"), {gw::CheckState::NotDone, {}});
            r.insert(QStringLiteral("nvidia"), {gw::CheckState::NotDone, {}});
            return r;
        };
        setup.readSetup = [this](const QString &, QString *, QStringList *) { return std::optional(m_saved); };
        setup.checkItems = [](const QList<const gw::Item *> &items) {
            gw::CheckResults r;
            for (const gw::Item *item : items)
                r.insert(item->id(), {gw::CheckState::NotDone, {}});
            return r;
        };
        setup.system.id = QStringLiteral("opensuse-tumbleweed");
        setup.language = language;
        setup.workerProgram = worker;
        setup.stateDir = m_dir.filePath(QStringLiteral("state"));
        setup.jobs = jobs;
        setup.liveSystem = live;
        setup.surveyDrives = fakeSurvey;
        setup.gatherSetup = [] {
            gw::Gathered found;
            found.packages = {QStringLiteral("act")};
            found.settings = {{QStringLiteral("Dolphin"), {QStringLiteral(".config/dolphinrc")}, false, 300},
                              {QStringLiteral("SSH keys"), {QStringLiteral(".ssh")}, true, 20}};
            return found;
        };
        setup.folderSpace = [this](const QString &) { return gw::FolderSpace{m_free, QStringLiteral("ext4")}; };
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
    // The setup job started from m_saved, on the saved setup page.
    std::unique_ptr<gw::Wizard> fromSaved()
    {
        auto w = make(QStringLiteral("en"), &restoring);
        if (!QTest::qWaitFor([&] { return w->checksDone(); }))
            return nullptr;
        auto *on = w->currentPage()->findChild<QCheckBox *>(QStringLiteral("fromSaved"));
        auto *file = w->currentPage()->findChild<QLineEdit *>(QStringLiteral("savedFile"));
        if (!on || !file)
            return nullptr;
        on->click();
        file->setText(m_dir.filePath(QStringLiteral("groundwork-setup-pc-2026-10-20.tar")));
        w->next();
        return w->currentPage()->title() == QLatin1String("Your saved setup") ? std::move(w) : nullptr;
    }
    static gw::ZypperSource source(const QString &alias, bool gpgcheck)
    {
        gw::ZypperSource s;
        s.alias = alias;
        s.name = alias;
        s.baseurl = QStringLiteral("https://download.opensuse.org/repositories/") + alias + QLatin1Char('/');
        s.gpgcheck = gpgcheck;
        return s;
    }
    static void goToRunPage(gw::Wizard &w)
    {
        for (int i = 0; i < 10 && w.currentPage() != w.runPage(); ++i)
            w.next();
    }

private slots:
    void cleanup()
    {
        gw::loadLanguage(QStringLiteral("en"));
        m_free = 100 * kGiB;
        m_saved = {};
        qunsetenv("FAKE_WORKER_LOG");
    }

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

    // GRND-0081: switching on an item that includes a switched-on one
    // switches it off and says why, until the includer is switched off.
    void switchingOnAnIncluderSwitchesOffWhatItIncludesAndSaysWhy()
    {
        auto w = make(QStringLiteral("en"), &including);
        QTRY_VERIFY(w->checksDone());
        auto *parserRow = w->row(QStringLiteral("parser"));
        parserRow->toggle()->click();
        QVERIFY(parserRow->isChecked());
        w->row(QStringLiteral("terminal"))->toggle()->click();
        QVERIFY(!parserRow->isChecked());
        QVERIFY2(parserRow->reason().contains(QLatin1String("Title of terminal includes it")),
                 qPrintable(parserRow->reason()));
        w->row(QStringLiteral("terminal"))->toggle()->click();
        QVERIFY2(parserRow->reason().isEmpty(), qPrintable(parserRow->reason()));
    }

    // GRND-0082: while an item that offers another is switched on, the
    // other's row says so, whether or not it is on itself.
    void anItemOfferedByASwitchedOnOneSaysSo()
    {
        auto w = make(QStringLiteral("en"), &offering);
        QTRY_VERIFY(w->checksDone());
        auto *assistantRow = w->row(QStringLiteral("assistant"));
        QVERIFY2(assistantRow->reason().isEmpty(), qPrintable(assistantRow->reason()));
        w->row(QStringLiteral("host"))->toggle()->click();
        QCOMPARE(assistantRow->reason(), QStringLiteral("Title of host offers this too."));
        assistantRow->toggle()->click();
        QVERIFY(assistantRow->isChecked());
        QCOMPARE(assistantRow->reason(), QStringLiteral("Title of host offers this too."));
        w->row(QStringLiteral("host"))->toggle()->click();
        QVERIFY2(assistantRow->reason().isEmpty(), qPrintable(assistantRow->reason()));
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

    // The first page says Groundwork is not an openSUSE or SUSE product
    // (user request, 2026-10-16).
    void theFirstPageSaysItIsIndependent()
    {
        auto w = make();
        auto *disclaimer = w->page(0)->findChild<QLabel *>(QStringLiteral("disclaimer"));
        QVERIFY(disclaimer);
        QVERIFY(!disclaimer->isHidden());
        QVERIFY(disclaimer->text().contains(QLatin1String("not endorsed")));
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

    // The carry job, from the first page to what was gathered.
    std::unique_ptr<gw::Wizard> gathered()
    {
        auto w = make(QStringLiteral("en"), &catalogue, {gw::Job::SetUp, gw::Job::CarryOver});
        w->chooseJob(gw::Job::CarryOver);
        if (!QTest::qWaitFor([&] { return w->checksDone(); }))
            return nullptr;
        w->next();
        if (!QTest::qWaitFor([&] { return w->currentPage()->isComplete(); }))
            return nullptr;
        return w;
    }

    // GRND-0078, INV-2: a secret starts switched off; switched on, it says
    // the file will hold its passwords or keys.
    void aSecretStartsOffAndWarnsWhenSwitchedOn()
    {
        auto w = gathered();
        QVERIFY(w);
        auto *dolphin = w->currentPage()->findChild<QCheckBox *>(QStringLiteral("setting:Dolphin"));
        auto *ssh = w->currentPage()->findChild<QCheckBox *>(QStringLiteral("setting:SSH keys"));
        auto *warning = w->currentPage()->findChild<QLabel *>(QStringLiteral("secretWarning:SSH keys"));
        QVERIFY(dolphin && ssh && warning);
        QVERIFY(dolphin->isChecked());
        QVERIFY(!ssh->isChecked());
        QVERIFY(warning->isHidden());
        ssh->click();
        QVERIFY(!warning->isHidden());
        QVERIFY(warning->text().contains(QLatin1String("Anyone with the file can read them.")));
        QVERIFY(!w->currentPage()->findChild<QLabel *>(QStringLiteral("secretWarning:Dolphin")));
    }

    // GRND-0078, spec 4.5: Save writes what the switches keep to the
    // choices file, and the Worker is started with it.
    void saveWritesTheChoicesAndStartsTheWorker()
    {
        auto w = gathered();
        QVERIFY(w);
        w->next(); // the save page
        QVERIFY(w->currentPage()->isComplete());
        w->next(); // Save
        QCOMPARE(w->currentPage(), w->carryRunPage());
        QVERIFY(w->carryArguments().contains(QStringLiteral("--save-setup")));
        QFile file(w->carryChoice().choicesPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject choices = QJsonDocument::fromJson(file.readAll()).object();
        QVERIFY(choices.value("output").toString().endsWith(QLatin1String(".tar")));
        const QJsonObject manifest = choices.value("manifest").toObject();
        QCOMPARE(manifest.value("format").toInt(), 1);
        QCOMPARE(manifest.value("packages").toArray(), QJsonArray{"act"});
        const QJsonArray settings = manifest.value("settings").toArray();
        QCOMPARE(settings.size(), 1); // the secret stayed off
        QCOMPARE(settings.first().toObject().value("name").toString(), QStringLiteral("Dolphin"));
    }

    // GRND-0078, INV-9 shown: Save waits for room, and says why.
    void saveWaitsForRoom()
    {
        m_free = 100; // bytes; cleanup() puts it back
        auto w = gathered();
        QVERIFY(w);
        w->next();
        QVERIFY(!w->currentPage()->isComplete());
        QLabel *problem = shownLabel(w->currentPage(), QStringLiteral("saveProblem"));
        QVERIFY(problem);
        QVERIFY2(problem->text().contains(QLatin1String("Dolphin")), qPrintable(problem->text()));
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

    // The user, 2026-10-16: on a drive with Windows, the layout page starts
    // on the choice that keeps it; wiping it takes a deliberate click.
    void aDriveWithWindowsStartsBesideIt()
    {
        auto w = pickDrive(QStringLiteral("/dev/sdc"));
        QVERIFY(w);
        w->next(); // past Windows' checklist
        auto *beside = w->currentPage()->findChild<QRadioButton *>(
            QStringLiteral("suggestion%1").arg(int(gw::Suggestion::Beside)));
        QVERIFY(beside);
        QVERIFY(beside->isChecked());
    }

    // GRND-0084, the user 2026-10-16: when room to hibernate grows swap past
    // the free space beside Windows, the page chooses nothing, rather than
    // falling back to the whole drive, which erases Windows.
    void besideWindowsThatStopsFittingChoosesNothing()
    {
        auto w = pickDrive(QStringLiteral("/dev/sdd"));
        QVERIFY(w);
        w->next(); // past Windows' checklist
        QWizardPage *page = w->currentPage();
        const QString besideName = QStringLiteral("suggestion%1").arg(int(gw::Suggestion::Beside));
        QVERIFY(page->findChild<QRadioButton *>(besideName)->isChecked());
        auto *hibernate = page->findChild<QCheckBox *>(QStringLiteral("hibernate"));
        QVERIFY(!hibernate->isChecked());
        hibernate->click();
        QVERIFY(!page->findChild<QRadioButton *>(besideName)); // no longer fits
        for (auto *pick : page->findChildren<QRadioButton *>())
            QVERIFY2(!pick->isChecked(), qPrintable(pick->objectName()));
        QVERIFY(!page->isComplete());
        QVERIFY(hibernate->isEnabled()); // so the user can turn it back off

        hibernate->click();
        QVERIFY(page->findChild<QRadioButton *>(besideName));
        QVERIFY(!page->isComplete()); // still the user's pick
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

    // GRND-0076's VM run: at the window's first size, the layout page
    // squeezed its wrapped text, overlapping the notes and cutting off the
    // last partition. Every page of the job shows each line at its full
    // height. A live system, as in the run: only this job's pages size the
    // window. The text is half as large again, as a larger desktop font
    // makes it.
    void thePreparePagesSqueezeNoText()
    {
        auto w = make(QStringLiteral("en"), &catalogue, {gw::Job::SetUp, gw::Job::PrepareDrive}, true);
        QFont large = w->font();
        large.setPointSizeF(large.pointSizeF() * 1.5);
        w->setFont(large);
        w->show();
        w->next();
        w->currentPage()->findChild<QRadioButton *>(QStringLiteral("/dev/sdc"))->click();
        w->next();
        for (int page = 0; page < 3; ++page) { // Windows' checklist, the layout, confirm
            if (page == 1) // the layout with the most lines
                w->currentPage()->findChild<QRadioButton *>(
                                     QStringLiteral("suggestion%1").arg(int(gw::Suggestion::Beside)))
                    ->click();
            if (page == 2)
                QVERIFY(w->currentPage()->isCommitPage()); // read it, never apply it
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            int checked = 0;
            for (QLabel *label : w->currentPage()->findChildren<QLabel *>()) {
                if (!label->isVisible() || !label->wordWrap())
                    continue;
                ++checked;
                QVERIFY2(label->height() >= label->heightForWidth(label->width()),
                         qPrintable(w->currentPage()->title() + QStringLiteral(": ") + label->text()));
            }
            QVERIFY(checked > 0);
            if (page < 2)
                w->next();
        }
    }

    // The pre-push gate, GRND-0076: a run page destroyed while its Worker's
    // output was still unread read that output during its own destruction,
    // after its rows were gone, and crashed.
    void aRunPageClosedWithOutputWaitingDoesNotCrash()
    {
        auto *page = new gw::RunPage;
        page->start(QStringLiteral("sh"),
                    {QStringLiteral("-c"), QStringLiteral("echo '@@STEP_BEGIN@@|a|1|1|a'; echo '@@HINT@@|a|b'; sleep 5")},
                    {{QStringLiteral("a"), QStringLiteral("A")}});
        QThread::msleep(300); // the lines wait in the pipe; no event reads them
        delete page;
    }

    // A Worker that cannot start ends the run, saying so; the page does not
    // wait for it. A second run says its own outcome, not the first one's.
    void aWorkerThatCannotStartEndsTheRun()
    {
        gw::RunPage page;
        QSignalSpy finished(&page, &gw::RunPage::runFinished);
        page.start(QStringLiteral("sh"), {QStringLiteral("-c"), QStringLiteral("echo '@@DONE@@|1|0|0'")}, {});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(page.summary(), gw::RunPage::tr("All done."));
        page.start(QStringLiteral("/nonexistent/groundwork-worker"), {}, {});
        QTRY_COMPARE(finished.size(), 2);
        QVERIFY(page.isComplete());
        QCOMPARE(page.summary(), gw::RunPage::tr("Groundwork stopped unexpectedly. The details below say why."));
    }

    // GRND-0076's VM run, in Arabic: the drive's partition lines put their
    // bullets on the wrong side and showed "/dev/vda3" as "dev/vda3/". Each
    // line runs the window's way, and its path, filesystem and label keep
    // their own direction.
    void aDrivesLinesReadRightToLeft()
    {
        QApplication::setLayoutDirection(Qt::RightToLeft);
        auto w = pickDrive(QStringLiteral("/dev/sdc"));
        QApplication::setLayoutDirection(Qt::LeftToRight);
        QVERIFY(w);
        w->back(); // the drive page, where the lines were read right to left
        bool seen = false;
        for (QLabel *label : w->currentPage()->findChildren<QLabel *>()) {
            if (!label->text().contains(QLatin1String("/dev/sdc1")))
                continue;
            seen = true;
            for (const QString &line : label->text().split(QLatin1Char('\n'))) {
                QVERIFY2(line.startsWith(QChar(0x200F)), qPrintable(line));
                if (line.contains(QLatin1String("/dev/sdc")))
                    QVERIFY2(line.contains(QRegularExpression(QStringLiteral(u"⁨/dev/sdc\\d*⁩"))),
                             qPrintable(line));
            }
        }
        QVERIFY(seen);
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

    // GRND-0079, INV-1: a kept item not done starts switched on on leaving
    // the saved setup page; one that accepts a licence waits, noted.
    void keptItemsStartOnButALicenceWaits()
    {
        m_saved.systemId = QStringLiteral("opensuse-tumbleweed");
        m_saved.items = {QStringLiteral("firewall"), QStringLiteral("nvidia"), QStringLiteral("gone-item")};
        auto w = fromSaved();
        QVERIFY(w);
        QVERIFY(!w->selection().contains(QStringLiteral("firewall")));
        bool named = false;
        for (auto *label : w->currentPage()->findChildren<QLabel *>())
            named = named || (label->text().startsWith(QLatin1String("Not offered by this version: "))
                              && label->text().contains(QLatin1String("gone-item")));
        QVERIFY(named);
        w->next();
        QVERIFY(w->selection().contains(QStringLiteral("firewall")));
        QVERIFY(!w->selection().contains(QStringLiteral("nvidia")));
        QCOMPARE(w->row(QStringLiteral("nvidia"))->reason(), QStringLiteral("You had this before."));
    }

    // GRND-0079's VM run, in Arabic and Hebrew: "26 Oct 2026 05:46:39"
    // showed as "Oct 2026 05:46:39 26", its day apart from the rest. The
    // date keeps its own direction, as the file's name does.
    void theSavedDateKeepsItsOwnDirection()
    {
        m_saved.created = QStringLiteral("2026-10-26T05:46:39Z");
        auto w = fromSaved();
        QVERIFY(w);
        const QString date =
            QLocale().toString(QDateTime::fromString(m_saved.created, Qt::ISODate).toLocalTime(), QLocale::ShortFormat);
        bool seen = false;
        for (auto *label : w->currentPage()->findChildren<QLabel *>()) {
            if (!label->text().startsWith(QLatin1String("File: ")))
                continue;
            seen = true;
            QVERIFY2(label->text().endsWith(QChar(0x2068) + date + QChar(0x2069)), qPrintable(label->text()));
        }
        QVERIFY(seen);
    }

    // GRND-0079, INV-8: a source with signatures unchecked, or from a file
    // saved on another system, starts switched off, saying why; programs
    // start on, with the update they need.
    void aDoubtfulSourceStartsOffSayingWhy()
    {
        m_saved.systemId = QStringLiteral("opensuse-leap");
        m_saved.packages = {QStringLiteral("act")};
        m_saved.zypperSources = {source(QStringLiteral("unsigned"), false), source(QStringLiteral("games"), true)};
        auto w = fromSaved();
        QVERIFY(w);
        QVERIFY(!w->selection().contains(QStringLiteral("carried-source:unsigned")));
        QVERIFY(!w->selection().contains(QStringLiteral("carried-source:games")));
        QVERIFY(w->row(QStringLiteral("carried-source:unsigned"))->reason().contains(QLatin1String("signatures")));
        QVERIFY(w->row(QStringLiteral("carried-source:games"))->reason().contains(QLatin1String("different system")));
        QVERIFY(w->selection().contains(QStringLiteral("carried-packages")));
        QVERIFY(w->selection().contains(QStringLiteral("update")));
        auto *act = w->currentPage()->findChild<QCheckBox *>(QStringLiteral("carried-packages/\u2068act\u2069"));
        QVERIFY(act && act->isChecked());
    }

    // GRND-0079, INV-9: after a run from a file holding secrets, nothing is
    // removed or deleted until a button is pressed.
    void secretsAreRemovedOrTheFileDeletedOnlyWhenPressed()
    {
        const QString log = m_dir.filePath(QStringLiteral("worker-calls"));
        QFile::remove(log);
        qputenv("FAKE_WORKER_LOG", log.toUtf8());
        m_saved.systemId = QStringLiteral("opensuse-tumbleweed");
        m_saved.settings = {{QStringLiteral("SSH keys"), {QStringLiteral(".ssh")}, true, 20}};
        auto w = fromSaved();
        QVERIFY(w);
        QVERIFY(w->currentPage()->findChild<QLabel *>(QStringLiteral("secretMark:SSH keys")));
        goToRunPage(*w);
        QVERIFY(QTest::qWaitFor([&] { return w->runPage()->isComplete(); }));
        auto calls = [&] {
            QFile f(log);
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)
                                               : QStringList();
        };
        QCOMPARE(calls().size(), 1);
        QVERIFY(calls().first().contains(QLatin1String("--setup-choices")));
        QFile choices(w->restoreChoicesPath());
        QVERIFY(choices.open(QIODevice::ReadOnly));
        QVERIFY(QJsonDocument::fromJson(choices.readAll()).object().value("manifest").toObject().contains("settings"));

        auto *offer = w->runPage()->findChild<QWidget *>(QStringLiteral("secretsOffer"));
        QVERIFY(offer && !offer->isHidden());
        w->runPage()->findChild<QPushButton *>(QStringLiteral("deleteSetupFile"))->click();
        auto *confirm = w->runPage()->findChild<QPushButton *>(QStringLiteral("confirmDelete"));
        QVERIFY(confirm && !confirm->isHidden());
        QCOMPARE(calls().size(), 1); // asked once more first
        w->runPage()->findChild<QPushButton *>(QStringLiteral("removeSecrets"))->click();
        QVERIFY(QTest::qWaitFor([&] { return calls().size() == 2 && w->runPage()->isComplete(); }));
        QVERIFY(calls().last().contains(QLatin1String("--remove-secrets")));
        QVERIFY(offer->isHidden());
    }

    // GRND-0079: confirming the deletion runs it.
    void deletingTheFileRunsOnceConfirmed()
    {
        const QString log = m_dir.filePath(QStringLiteral("worker-calls"));
        QFile::remove(log);
        qputenv("FAKE_WORKER_LOG", log.toUtf8());
        m_saved.systemId = QStringLiteral("opensuse-tumbleweed");
        m_saved.settings = {{QStringLiteral("SSH keys"), {QStringLiteral(".ssh")}, true, 20}};
        auto w = fromSaved();
        QVERIFY(w);
        goToRunPage(*w);
        QVERIFY(QTest::qWaitFor([&] { return w->runPage()->isComplete(); }));
        w->runPage()->findChild<QPushButton *>(QStringLiteral("deleteSetupFile"))->click();
        w->runPage()->findChild<QPushButton *>(QStringLiteral("confirmDelete"))->click();
        QFile f(log);
        QVERIFY(QTest::qWaitFor([&] {
            return w->runPage()->isComplete() && f.open(QIODevice::ReadOnly)
                   && QString::fromUtf8(f.readAll()).contains(QLatin1String("--delete-setup-file"));
        }));
    }
};

QTEST_MAIN(TstWizard)
#include "tst_wizard.moc"
