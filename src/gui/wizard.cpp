#include "wizard.h"

#include "core/statepaths.h"
#include "core/translations.h"
#include "itemrow.h"
#include "runpage.h"
#include "savedsetuppage.h"
#include "switch.h"
#include "themes.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHash>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QStyle>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace gw {
namespace {

QString levelTitle(Level level)
{
    switch (level) {
    case Level::Essentials: return Wizard::tr("Essentials");
    case Level::SystemSetup: return Wizard::tr("System setup");
    case Level::Configuration: return Wizard::tr("Configuration");
    case Level::NiceToHave: return Wizard::tr("Nice to have");
    }
    return {};
}

QString levelSubTitle(Level level)
{
    switch (level) {
    case Level::Essentials: return Wizard::tr("What a desktop needs for things to work.");
    case Level::SystemSetup: return Wizard::tr("That the system can recover and is protected.");
    case Level::Configuration: return Wizard::tr("Choices a new install asks of its owner.");
    case Level::NiceToHave: return Wizard::tr("Extras. Each one is its own choice.");
    }
    return {};
}

QString jobTitle(Job job)
{
    switch (job) {
    case Job::SetUp: return Wizard::tr("Set up this computer");
    case Job::CarryOver: return Wizard::tr("Carry this setup over to a new install");
    case Job::PrepareDrive: return Wizard::tr("Prepare a drive for installing openSUSE");
    }
    return {};
}

class WelcomePage : public QWizardPage
{
public:
    WelcomePage(Wizard *wizard, const QString &language) : QWizardPage(wizard), m_wizard(wizard)
    {
        setTitle(Wizard::tr("Welcome to Groundwork"));
        auto *intro = new QLabel(Wizard::tr("Groundwork sets up this computer the way you want. "
                                            "Each page offers a level of choices, from essentials to "
                                            "extras. Nothing changes until you press Apply."), this);
        intro->setWordWrap(true);
        auto *languageLabel = new QLabel(Wizard::tr("Language:"), this);
        auto *languages = new QComboBox(this);
        languageLabel->setBuddy(languages);
        for (const QString &code : availableLanguages()) {
            QString name = code == QLatin1String("en") ? QStringLiteral("English")
                                                       : QLocale(code).nativeLanguageName();
            const bool shows = fontShowsLanguage(code);
            if (!shows) {
                // Its own name would show as empty boxes, so it is named in
                // English and cannot be chosen.
                const QLocale locale(code);
                QString english = QLocale::languageToString(locale.language());
                if (code.contains(QLatin1Char('_')))
                    english += QStringLiteral(", ") + QLocale::territoryToString(locale.territory());
                name = Wizard::tr("%1 (no font installed)").arg(english);
            } else if (isDraft(code)) {
                name = Wizard::tr("%1 (draft)").arg(name);
            }
            languages->addItem(name, code);
            if (!shows)
                static_cast<QStandardItemModel *>(languages->model())->item(languages->count() - 1)->setEnabled(false);
        }
        // A draft in use says so in its own words, so its reader can tell.
        QLabel *draftNotice = nullptr;
        if (isDraft(language)) {
            draftNotice = new QLabel(
                Wizard::tr("This translation is a draft: no native speaker has checked it yet."), this);
            draftNotice->setObjectName(QStringLiteral("draftNotice"));
            draftNotice->setWordWrap(true);
        }
        languages->setCurrentIndex(qMax(0, languages->findData(language)));
        connect(languages, &QComboBox::currentIndexChanged, this, [this, languages] {
            emit m_wizard->languageChangeRequested(languages->currentData().toString());
        });
        // Colours change in place; nothing is rebuilt (GRND-0050).
        auto *themeLabel = new QLabel(Wizard::tr("Colours:"), this);
        auto *themeChoice = new QComboBox(this);
        themeChoice->setObjectName(QStringLiteral("themes"));
        themeLabel->setBuddy(themeChoice);
        for (const Theme &theme : themes())
            themeChoice->addItem(theme.name, theme.id);
        themeChoice->setCurrentIndex(qMax(0, themeChoice->findData(currentTheme())));
        connect(themeChoice, &QComboBox::currentIndexChanged, this, [themeChoice] {
            const QString id = themeChoice->currentData().toString();
            applyTheme(id);
            // The password box is another process; it reads this.
            qputenv("GROUNDWORK_THEME", id.toUtf8());
        });
        // More than one job: the user picks one (GRND-0077).
        QLabel *jobsLabel = nullptr;
        QWidget *jobs = nullptr;
        if (wizard->offeredJobs().size() > 1) {
            jobsLabel = new QLabel(Wizard::tr("What would you like to do?"), this);
            jobs = new QWidget(this);
            jobs->setObjectName(QStringLiteral("jobs"));
            auto *jobsLayout = new QVBoxLayout(jobs);
            jobsLayout->setContentsMargins(0, 0, 0, 0);
            auto *group = new QButtonGroup(jobs);
            for (Job job : wizard->offeredJobs()) {
                auto *choice = new QRadioButton(jobTitle(job), jobs);
                choice->setChecked(wizard->chosenJob() == job);
                group->addButton(choice);
                jobsLayout->addWidget(choice);
                connect(choice, &QRadioButton::toggled, this, [this, job](bool on) {
                    if (!on)
                        return;
                    m_wizard->chooseJob(job);
                    if (m_saved)
                        m_saved->setVisible(job == Job::SetUp);
                    emit completeChanged();
                });
            }
        }
        // The setup job may start from a saved file (GRND-0079 spec 4.1).
        if (wizard->offeredJobs().contains(Job::SetUp) && wizard->offersSavedSetup())
            makeSavedChoice();
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        if (wizard->offeredJobs().contains(Job::SetUp) || wizard->offeredJobs().contains(Job::CarryOver))
            m_status->setText(Wizard::tr("Checking this computer…"));
        else if (wizard->offeredJobs().isEmpty())
            m_status->setText(Wizard::tr("This is a live system, started from a USB stick or a disc. "
                                         "Anything set up here is gone after a restart, so Groundwork "
                                         "has nothing to do here."));
        connect(wizard, &Wizard::checksFinished, this, [this] {
            m_status->setText(Wizard::tr("Checked. Press Next to choose what to set up."));
            emit completeChanged();
        });

        auto *layout = new QVBoxLayout(this);
        layout->addWidget(intro);
        layout->addWidget(languageLabel);
        layout->addWidget(languages);
        if (draftNotice)
            layout->addWidget(draftNotice);
        layout->addWidget(themeLabel);
        layout->addWidget(themeChoice);
        if (jobs) {
            layout->addWidget(jobsLabel);
            layout->addWidget(jobs);
        }
        if (m_saved)
            layout->addWidget(m_saved);
        layout->addWidget(m_status);
        layout->addStretch();
        auto *disclaimer = new QLabel(Wizard::tr("Groundwork is an independent project, not an official openSUSE "
                                                 "or SUSE product, and is not endorsed by the openSUSE Project or "
                                                 "SUSE."), this);
        disclaimer->setObjectName(QStringLiteral("disclaimer"));
        disclaimer->setWordWrap(true);
        layout->addWidget(disclaimer);
    }
    // Carrying a setup over records the items done, so it waits for the
    // checks too. Preparing a drive needs none.
    bool isComplete() const override
    {
        return !m_reading
               && (((m_wizard->chosenJob() == Job::SetUp || m_wizard->chosenJob() == Job::CarryOver)
                    && m_wizard->checksDone())
                   || m_wizard->chosenJob() == Job::PrepareDrive);
    }
    // A saved file is read on Next; one refused keeps the page, saying why.
    bool validatePage() override
    {
        if (m_wizard->chosenJob() != Job::SetUp || !m_fromSaved || !m_fromSaved->isChecked()) {
            m_wizard->clearSavedSetup();
            return true;
        }
        const QString file = m_savedFile->text().trimmed();
        if (m_wizard->hasSavedSetup(file))
            return true; // read already; the switches keep what the user chose
        if (m_reading)
            return false;
        // Read off the window's thread; Next waits, then moves on.
        m_reading = true;
        m_savedProblem->hide();
        emit completeChanged();
        m_wizard->loadSavedSetup(file, [this, file](bool read, const QString &why) {
            m_reading = false;
            emit completeChanged();
            m_savedProblem->setText(why);
            m_savedProblem->setVisible(!read);
            if (read && m_wizard->currentPage() == this && m_fromSaved->isChecked()
                && m_savedFile->text().trimmed() == file)
                m_wizard->next();
        });
        return false;
    }

private:
    void makeSavedChoice()
    {
        m_saved = new QWidget(this);
        m_saved->setVisible(m_wizard->chosenJob() == Job::SetUp);
        m_fromSaved = new Switch(Wizard::tr("Start from a saved setup"), m_saved);
        m_fromSaved->setObjectName(QStringLiteral("fromSaved"));
        m_savedFile = new QLineEdit(m_saved);
        m_savedFile->setObjectName(QStringLiteral("savedFile"));
        m_savedFile->setAccessibleName(Wizard::tr("Saved setup file"));
        auto *browse = new QPushButton(Wizard::tr("Browse…"), m_saved);
        m_savedProblem = new QLabel(m_saved);
        m_savedProblem->setObjectName(QStringLiteral("savedProblem"));
        m_savedProblem->setWordWrap(true);
        m_savedProblem->hide();
        for (QWidget *w : {static_cast<QWidget *>(m_savedFile), static_cast<QWidget *>(browse)})
            w->setEnabled(false);
        connect(m_fromSaved, &QCheckBox::toggled, m_savedFile, &QWidget::setEnabled);
        connect(m_fromSaved, &QCheckBox::toggled, browse, &QWidget::setEnabled);
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString file = QFileDialog::getOpenFileName(this, Wizard::tr("Choose a saved setup"), usualSetupFolder(),
                                                              Wizard::tr("Saved setups (groundwork-setup-*.tar)"));
            if (!file.isEmpty())
                m_savedFile->setText(file);
        });
        auto *fileRow = new QHBoxLayout;
        fileRow->addWidget(m_savedFile, 1);
        fileRow->addWidget(browse);
        auto *layout = new QVBoxLayout(m_saved);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_fromSaved);
        layout->addLayout(fileRow);
        layout->addWidget(m_savedProblem);
    }

    Wizard *m_wizard;
    QLabel *m_status;
    QWidget *m_saved = nullptr;
    QCheckBox *m_fromSaved = nullptr;
    QLineEdit *m_savedFile = nullptr;
    QLabel *m_savedProblem = nullptr;
    bool m_reading = false; // a saved file is being read
};

class ReviewPage : public QWizardPage
{
public:
    explicit ReviewPage(Wizard *wizard) : QWizardPage(wizard), m_wizard(wizard), m_list(new QLabel(this)), m_error(new QLabel(this))
    {
        setTitle(Wizard::tr("Ready to apply"));
        setCommitPage(true);
        setButtonText(QWizard::CommitButton, Wizard::tr("Apply"));
        m_list->setWordWrap(true);
        m_list->setTextFormat(Qt::PlainText);
        m_error->setWordWrap(true);
        m_error->hide();
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(m_list);
        layout->addWidget(m_error);
        layout->addStretch();
    }
    void initializePage() override
    {
        m_error->hide();
        const QStringList order = m_wizard->runOrder();
        if (order.isEmpty()) {
            setSubTitle(Wizard::tr("Nothing is switched on. Go back to choose what to set up."));
            m_list->clear();
            return;
        }
        setSubTitle(Wizard::tr("These run in this order after one password."));
        QStringList lines;
        for (const QString &id : order) {
            const Item *item = m_wizard->catalogue().find(id);
            lines << QStringLiteral("• ") + item->title() + QStringLiteral(" — ") + item->applySentence();
        }
        m_list->setText(lines.join(QLatin1Char('\n')));
    }
    // Apply is unavailable until an item is switched on (design, Worker's interface).
    bool isComplete() const override { return !m_wizard->runOrder().isEmpty() && m_wizard->valuesValid(); }
    // What stays on from a saved file goes to the Worker in a file (spec 4.6).
    bool validatePage() override
    {
        if (!m_wizard->savedSetup() || m_wizard->writeRestoreChoices())
            return true;
        m_error->setText(Wizard::tr("What to restore could not be written to %1, so nothing was changed.")
                             .arg(isolated(m_wizard->restoreChoicesPath())));
        m_error->show();
        return false;
    }

private:
    Wizard *m_wizard;
    QLabel *m_list;
    QLabel *m_error;
};

class RunWizardPage : public RunPage
{
public:
    explicit RunWizardPage(Wizard *wizard) : RunPage(wizard), m_wizard(wizard) { makeSecretsOffer(); }
    void initializePage() override
    {
        QList<QPair<QString, QString>> titles;
        for (const QString &id : m_wizard->runOrder())
            titles.append({id, m_wizard->row(id) ? rowTitle(id) : id});
        start(m_program, m_wizard->workerArguments(), titles);
    }
    void configure(const QString &program) { m_program = program; }

private:
    QString rowTitle(const QString &id) const { return m_wizard->row(id)->toggle()->text(); }
    // A saved file holding secrets: after the run, the user may remove them
    // from it or delete it. Neither runs unless pressed (spec 4.8, INV-9).
    void makeSecretsOffer()
    {
        auto *offer = new QWidget(this);
        offer->setObjectName(QStringLiteral("secretsOffer"));
        offer->hide();
        auto *text = new QLabel(Wizard::tr("This saved file holds saved passwords or keys. Anyone with the file can "
                                           "read them."),
                                offer);
        text->setWordWrap(true);
        auto *remove = new QPushButton(Wizard::tr("Remove them from the file"), offer);
        remove->setObjectName(QStringLiteral("removeSecrets"));
        auto *erase = new QPushButton(Wizard::tr("Delete the file"), offer);
        erase->setObjectName(QStringLiteral("deleteSetupFile"));
        // Asked once more, on the page, naming the file.
        auto *confirm = new QWidget(offer);
        confirm->hide();
        auto *confirmText = new QLabel(confirm);
        confirmText->setWordWrap(true);
        auto *yes = new QPushButton(Wizard::tr("Yes, delete it"), confirm);
        yes->setObjectName(QStringLiteral("confirmDelete"));
        auto *confirmLayout = new QVBoxLayout(confirm);
        confirmLayout->setContentsMargins(0, 0, 0, 0);
        confirmLayout->addWidget(confirmText);
        confirmLayout->addWidget(yes, 0, Qt::AlignLeading);
        auto *layout = new QVBoxLayout(offer);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(text);
        layout->addWidget(remove, 0, Qt::AlignLeading);
        layout->addWidget(erase, 0, Qt::AlignLeading);
        layout->addWidget(confirm);
        static_cast<QVBoxLayout *>(this->layout())->addWidget(offer);

        connect(this, &RunPage::runFinished, offer, [this, offer] {
            const SavedSetup *saved = m_wizard->savedSetup();
            if (m_offered || !saved || !saved->holdsSecrets())
                return;
            m_offered = true;
            offer->show();
        });
        connect(remove, &QPushButton::clicked, offer, [this, offer] {
            offer->hide();
            start(m_program, m_wizard->secretsArguments(true),
                  {{QStringLiteral("remove-secrets"), Wizard::tr("Removing saved passwords and keys")}});
        });
        connect(erase, &QPushButton::clicked, confirm, [this, confirm, confirmText] {
            confirmText->setText(Wizard::tr("Delete %1? It cannot be undone.")
                                     .arg(isolated(QFileInfo(m_wizard->savedSetup()->file).fileName())));
            confirm->show();
        });
        connect(yes, &QPushButton::clicked, offer, [this, offer] {
            offer->hide();
            start(m_program, m_wizard->secretsArguments(false),
                  {{QStringLiteral("delete-setup-file"), Wizard::tr("Deleting the saved file")}});
        });
    }

    Wizard *m_wizard;
    QString m_program;
    bool m_offered = false;
};

class PrepareRunWizardPage : public RunPage
{
public:
    explicit PrepareRunWizardPage(Wizard *wizard) : RunPage(wizard), m_wizard(wizard)
    {
        setTitle(Wizard::tr("Preparing the drive"));
        setOffersOneUp(false);
    }
    void initializePage() override
    {
        start(m_program, m_wizard->prepareArguments(), {{QStringLiteral("prepare-drive"), title()}});
    }
    void configure(const QString &program) { m_program = program; }

private:
    Wizard *m_wizard;
    QString m_program;
};

class CarryRunWizardPage : public RunPage
{
public:
    explicit CarryRunWizardPage(Wizard *wizard) : RunPage(wizard), m_wizard(wizard)
    {
        setTitle(Wizard::tr("Saving the setup"));
        setOffersOneUp(false);
    }
    void initializePage() override
    {
        start(m_program, m_wizard->carryArguments(), {{QStringLiteral("save-setup"), title()}});
    }
    void configure(const QString &program) { m_program = program; }

private:
    Wizard *m_wizard;
    QString m_program;
};

} // namespace

bool fontShowsLanguage(const QString &code)
{
    // Scripts the default font set may lack; Latin, Arabic and Hebrew come
    // with DejaVu, but are checked the same way.
    static const QHash<QLocale::Script, QFontDatabase::WritingSystem> kNeeds{
        {QLocale::SimplifiedHanScript, QFontDatabase::SimplifiedChinese},
        {QLocale::TraditionalHanScript, QFontDatabase::TraditionalChinese},
        {QLocale::JapaneseScript, QFontDatabase::Japanese},
        {QLocale::KoreanScript, QFontDatabase::Korean},
        {QLocale::DevanagariScript, QFontDatabase::Devanagari},
        {QLocale::ArabicScript, QFontDatabase::Arabic},
        {QLocale::HebrewScript, QFontDatabase::Hebrew},
    };
    const auto need = kNeeds.constFind(QLocale(code).script());
    return need == kNeeds.cend() || !QFontDatabase::families(*need).isEmpty();
}

void useFontsOwnBold()
{
    // Measured on Noto Sans CJK, a variable font: Qt picks its Bold, then
    // thickens it again. Fonts with no Bold of their own now show regular.
    qputenv("QT_NO_SYNTHESIZED_BOLD", "1");
}

QFont fontFor(const QString &code, const QFont &base)
{
    // First, not as a fallback: Qt keeps the first fallback face it finds
    // for a family, so a second language would get the first one's shapes.
    static const QHash<QLocale::Script, QString> kFaces{
        {QLocale::SimplifiedHanScript, QStringLiteral("Noto Sans CJK SC")},
        {QLocale::TraditionalHanScript, QStringLiteral("Noto Sans CJK TC")},
        {QLocale::JapaneseScript, QStringLiteral("Noto Sans CJK JP")},
        {QLocale::KoreanScript, QStringLiteral("Noto Sans CJK KR")},
    };
    const auto face = kFaces.constFind(QLocale(code).script());
    if (face == kFaces.cend() || !QFontDatabase::families().contains(*face))
        return base;
    QFont font = base;
    font.setFamilies({*face, base.family()});
    return font;
}

Wizard::Wizard(WizardSetup setup, QWidget *parent) : QWizard(parent), m_setup(std::move(setup))
{
    setWindowTitle(tr("Groundwork"));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnLastPage);
    setOption(QWizard::NoCancelButtonOnLastPage, false);
    // Labelled here rather than left to Qt, which has no words of its own
    // for some languages offered, such as Afrikaans. The English is Qt's.
    setButtonText(QWizard::BackButton, tr("< &Back"));
    setButtonText(QWizard::NextButton, tr("&Next >"));
    setButtonText(QWizard::FinishButton, tr("&Finish"));
    setButtonText(QWizard::CancelButton, tr("Cancel"));

    for (Job job : std::as_const(m_setup.jobs)) {
        if (!m_setup.liveSystem || job == Job::PrepareDrive)
            m_offeredJobs << job;
    }
    if (!m_offeredJobs.isEmpty())
        m_chosenJob = m_offeredJobs.first();
    addPage(new WelcomePage(this, m_setup.language));
    for (Level level : {Level::Essentials, Level::SystemSetup, Level::Configuration, Level::NiceToHave}) {
        const QList<const Item *> &items = m_setup.catalogue->items();
        if (std::none_of(items.cbegin(), items.cend(), [level](const Item *item) { return item->level() == level; }))
            continue; // a level with no items gets no page
        auto *page = new QWizardPage(this);
        page->setTitle(levelTitle(level));
        page->setSubTitle(levelSubTitle(level));
        auto *inner = new QWidget;
        new QVBoxLayout(inner);
        auto *scroll = new QScrollArea(page);
        scroll->setWidgetResizable(true);
        scroll->setWidget(inner);
        auto *layout = new QVBoxLayout(page);
        layout->addWidget(scroll);
        const int id = addPage(page);
        if (m_firstLevelId < 0)
            m_firstLevelId = id;
        m_levelPages.insert(int(level), inner);
    }
    addPage(new ReviewPage(this));
    auto *run = new RunWizardPage(this);
    run->configure(m_setup.workerProgram);
    m_runPage = run;
    addPage(run);
    if (m_offeredJobs.contains(Job::PrepareDrive)) {
        m_prepare.survey = m_setup.surveyDrives;
        m_prepare.layoutPath = m_setup.stateDir + QStringLiteral("/layout.json");
        m_drivePageId = addPage(new DrivePage(&m_prepare, this));
        m_windowsPageId = addPage(new WindowsPage(&m_prepare, this));
        auto *layoutPage = new LayoutPage(&m_prepare, this);
        m_layoutPageId = addPage(layoutPage);
        // Its list of partitions changes with the choice.
        connect(layoutPage, &QWizardPage::completeChanged, this, &Wizard::fitRows);
        addPage(new ConfirmPage(&m_prepare, this));
        auto *prepareRun = new PrepareRunWizardPage(this);
        prepareRun->configure(m_setup.workerProgram);
        m_prepareRunPage = prepareRun;
        addPage(prepareRun);
    }
    if (m_offeredJobs.contains(Job::CarryOver)) {
        m_carry.gather = m_setup.gatherSetup;
        m_carry.space = m_setup.folderSpace;
        m_carry.choicesPath = m_setup.stateDir + QStringLiteral("/setup-choices.json");
        m_carry.version = QCoreApplication::applicationVersion();
        m_carry.system = m_setup.system;
        auto *gather = new GatherPage(&m_carry, this);
        m_gatherPageId = addPage(gather);
        // Its rows arrive once gathered.
        connect(gather, &QWizardPage::completeChanged, this, &Wizard::fitRows);
        addPage(new SavePage(&m_carry, this));
        auto *carryRun = new CarryRunWizardPage(this);
        carryRun->configure(m_setup.workerProgram);
        m_carryRunPage = carryRun;
        addPage(carryRun);
    }
    if (m_offeredJobs.contains(Job::SetUp) && offersSavedSetup())
        m_savedPageId = addPage(new SavedSetupPage(this));
    connect(this, &QWizard::currentIdChanged, this, &Wizard::fitRows);
    for (RunPage *page : {m_runPage, m_prepareRunPage, m_carryRunPage}) {
        if (!page)
            continue;
        connect(page, &RunPage::runFinished, this, [this] {
            if (!isVisible()) // closed during the run: quit once the Worker is done
                qApp->quit();
        });
    }

    if (!m_offeredJobs.contains(Job::SetUp) && !m_offeredJobs.contains(Job::CarryOver))
        return; // nothing here is set up or carried, so nothing is checked
    // Checks run off the window's thread so the window stays responsive.
    // The thread holds its own copy of the checks, and its results reach
    // this window only while it exists: changing the language replaces
    // the window while the checks may still run.
    auto results = std::make_shared<CheckResults>();
    QThread *thread = QThread::create([runChecks = m_setup.runChecks, results] { *results = runChecks(); });
    connect(thread, &QThread::finished, this, [this, results] { showResults(*results); });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Wizard::showResults(const CheckResults &results)
{
    m_results = results;
    for (const Item *item : m_setup.catalogue->items()) {
        QWidget *inner = m_levelPages.value(int(item->level()));
        auto *row = new ItemRow(*item, results.value(item->id()), inner);
        inner->layout()->addWidget(row);
        m_rows.insert(item->id(), row);
        connect(row, &ItemRow::toggledByUser, this, &Wizard::userToggled);
    }
    for (QWidget *inner : std::as_const(m_levelPages))
        static_cast<QVBoxLayout *>(inner->layout())->addStretch();
    m_selection = m_setup.catalogue->defaultSelection(results);
    refreshRows({}, {}, {}, true);
    for (const QString &id : gatherItems(*m_setup.catalogue, results))
        m_carry.items.append({id, m_setup.catalogue->find(id)->title()});
    m_checksDone = true;
    emit checksFinished();
}

void Wizard::fitRows()
{
    // The window opens at the welcome page's size, which shows a level's
    // rows only after scrolling (GRND-0049). On a level page, where the
    // header is its own height, grow it to the fullest level, within the
    // screen. A page of preparing a drive grows to show itself whole.
    if (!currentPage())
        return;
    auto *shown = currentPage()->findChild<QScrollArea *>();
    if (!shown)
        return;
    QList<QWidget *> fitted = m_levelPages.values();
    if (!fitted.contains(shown->widget())) {
        if (shown->objectName() != QLatin1String("preparePage") && shown->objectName() != QLatin1String("carryPage"))
            return;
        fitted = {shown->widget()};
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest); // settle the shown page
    const int viewWidth = shown->viewport()->width();
    int extra = 0;
    for (QWidget *inner : std::as_const(fitted)) {
        const int need = inner->hasHeightForWidth() ? inner->heightForWidth(viewWidth) : inner->sizeHint().height();
        extra = qMax(extra, need - shown->viewport()->height());
    }
    if (extra <= 0)
        return;
    const QRect screen = this->screen()->availableGeometry();
    // Wayland draws the title bar outside Qt's sight, so leave room for one.
    const int frame = qMax(frameGeometry().height() - height(),
                           style()->pixelMetric(QStyle::PM_TitleBarHeight, nullptr, this));
    resize(width(), qMin(height() + extra, screen.height() - frame));
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        // Wayland lets no window move itself, so the grown window kept its top
        // edge and ran off the screen, buttons and all (GRND-0075). Shown
        // again, it is placed afresh by the desktop at its new size.
        hide();
        show();
    } else if (frameGeometry().bottom() > screen.bottom()) {
        move(x(), screen.top());
    }
}

void Wizard::userToggled(const QString &id, bool on)
{
    if (on && m_saved && isCarriedId(id)) {
        switchOnSaved(id);
        return;
    }
    QStringList included;
    const QStringList pulled = on ? catalogue().switchOn(m_selection, id, m_results, &included)
                                  : catalogue().switchOff(m_selection, id);
    refreshRows(pulled, included, id, on);
}

void Wizard::refreshRows(const QStringList &pulled, const QStringList &included, const QString &cause, bool on)
{
    for (ItemRow *row : std::as_const(m_rows))
        row->setChecked(m_selection.contains(row->id()));
    const QString causeTitle = cause.isEmpty() ? QString() : m_rows.value(cause)->toggle()->text();
    for (const QString &id : pulled) {
        if (ItemRow *row = m_rows.value(id)) {
            row->setReason(on ? tr("Switched on because %1 needs it.").arg(causeTitle)
                              : tr("Switched off because it needs %1.").arg(causeTitle));
            m_reasons.insert(id, {cause, on, on});
        }
    }
    for (const QString &id : included) {
        if (ItemRow *row = m_rows.value(id)) {
            row->setReason(tr("Switched off because %1 includes it.").arg(causeTitle));
            m_reasons.insert(id, {cause, false, true});
        }
    }
    for (auto it = m_reasons.begin(); it != m_reasons.end();) {
        const Reason &reason = it.value();
        if (m_selection.contains(it.key()) == reason.rowOn && m_selection.contains(reason.cause) == reason.causeOn) {
            ++it;
            continue;
        }
        if (ItemRow *row = m_rows.value(it.key()))
            row->setReason({});
        it = m_reasons.erase(it);
    }
    // A switched-on item that offers this one too says so, unless the row
    // already gives a reason above.
    for (ItemRow *row : std::as_const(m_rows)) {
        const Item *item = catalogue().find(row->id());
        if (!item || item->alsoOfferedBy().isEmpty() || m_reasons.contains(row->id()))
            continue;
        QString offer;
        for (const QString &by : item->alsoOfferedBy())
            if (m_selection.contains(by) && m_rows.contains(by)) {
                offer = tr("%1 offers this too.").arg(m_rows.value(by)->toggle()->text());
                break;
            }
        row->setReason(offer);
    }
}

QStringList Wizard::workerArguments() const
{
    QStringList args = m_setup.workerPrefix
        + QStringList{QStringLiteral("--worker"), QStringLiteral("--lang"), m_setup.language};
    if (m_saved)
        args << QStringLiteral("--setup-choices") << restoreChoicesPath();
    const QStringList order = runOrder();
    for (const QString &id : order) {
        const ItemRow *row = m_rows.value(id);
        if (row && row->valueField())
            args << QStringLiteral("--set") << id + QLatin1Char('=') + row->value();
    }
    return args + order;
}

QStringList Wizard::prepareArguments() const
{
    return m_setup.workerPrefix
           + QStringList{QStringLiteral("--worker"), QStringLiteral("--prepare-drive"), m_prepare.layoutPath,
                         QStringLiteral("--lang"), m_setup.language};
}

QStringList Wizard::carryArguments() const
{
    return m_setup.workerPrefix
           + QStringList{QStringLiteral("--worker"), QStringLiteral("--save-setup"), m_carry.choicesPath,
                         QStringLiteral("--lang"), m_setup.language};
}

int Wizard::nextId() const
{
    // Set up: the welcome page, the levels, review, run. Prepare: the
    // welcome page, the drive, Windows' checklist where Windows is on it,
    // the layout, confirm, run (docs/design.md, Preparing a drive). Carry:
    // the welcome page, what to carry, where to save it, run.
    const int id = currentId();
    if (id == 0 && m_chosenJob == Job::PrepareDrive)
        return m_drivePageId;
    if (id == 0 && m_chosenJob == Job::CarryOver)
        return m_gatherPageId;
    if (id == 0 && m_chosenJob == Job::SetUp && m_saved)
        return m_savedPageId;
    if (id >= 0 && id == m_savedPageId)
        return m_firstLevelId;
    if (currentPage() == m_runPage || currentPage() == m_prepareRunPage || currentPage() == m_carryRunPage)
        return -1;
    if (id >= 0 && id == m_drivePageId)
        return m_prepare.drive && m_prepare.drive->hasWindows() ? m_windowsPageId : m_layoutPageId;
    return QWizard::nextId();
}

bool Wizard::valuesValid() const
{
    for (const QString &id : runOrder()) {
        const ItemRow *row = m_rows.value(id);
        if (row && row->valueField() && !catalogue().find(id)->isValidValue(row->value()))
            return false;
    }
    return true;
}

bool SavedSetup::holdsSecrets() const
{
    return std::any_of(manifest.settings.cbegin(), manifest.settings.cend(),
                       [](const SettingsRow &row) { return row.secret; });
}

const Catalogue &Wizard::catalogue() const { return m_saved ? *m_saved->all : *m_setup.catalogue; }

QStringList Wizard::runOrder() const
{
    QStringList order = catalogue().runOrder(m_selection);
    if (!m_saved)
        return order;
    order.removeIf([this](const QString &id) {
        return isCarriedId(id) && std::none_of(m_saved->entries.cbegin(), m_saved->entries.cend(), [&](const auto &e) {
                   return e.id == id && (!e.on || e.on->isChecked());
               });
    });
    return order;
}

void Wizard::loadSavedSetup(const QString &file, std::function<void(bool read, const QString &why)> done)
{
    if (file.isEmpty()) {
        done(false, tr("Choose the saved setup file first."));
        return;
    }
    // Reading runs tar over the file and the checks compare its archive,
    // which on a stick can take minutes, so both run off the window's
    // thread as the startup checks do. The thread holds its own copies of
    // the reader and the checks, and reads the catalogue, which outlives
    // every window; its result reaches this window only while it exists.
    struct Read
    {
        std::optional<Manifest> manifest;
        QString why;
        QStringList skipped;
        CarriedItems carried;
        CheckResults found;
    };
    auto read = std::make_shared<Read>();
    QThread *thread = QThread::create(
        [read, file, readSetup = m_setup.readSetup, checkItems = m_setup.checkItems, catalogue = m_setup.catalogue] {
            read->manifest = readSetup(file, &read->why, &read->skipped);
            if (!read->manifest)
                return;
            read->carried = carriedItems(*read->manifest, file, *catalogue);
            QList<const Item *> items;
            for (const std::unique_ptr<Item> &item : read->carried.owned)
                items << item.get();
            if (checkItems)
                read->found = checkItems(items);
        });
    connect(thread, &QThread::finished, this, [this, read, file, done = std::move(done)] {
        if (!read->manifest) {
            done(false, read->why);
            return;
        }
        clearSavedSetup();
        auto saved = std::make_unique<SavedSetup>();
        saved->file = file;
        saved->manifest = *read->manifest;
        saved->skipped = read->skipped;
        saved->carried = std::move(read->carried);
        saved->all.emplace(withCarried(*m_setup.catalogue, saved->carried));
        for (auto it = read->found.cbegin(); it != read->found.cend(); ++it)
            m_results.insert(it.key(), it.value());
        m_saved = std::move(saved);
        done(true, {});
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void Wizard::clearSavedSetup()
{
    if (!m_saved)
        return;
    // The page deletes the rows when it is made afresh.
    for (const std::unique_ptr<Item> &item : m_saved->carried.owned) {
        m_selection.remove(item->id());
        m_rows.remove(item->id());
        m_results.remove(item->id());
    }
    // What it switched on goes too, unless a choice that stays needs it.
    Selection drop = m_saved->switchedOn & m_selection;
    for (bool needed = true; needed;) {
        needed = false;
        for (const QString &id : std::as_const(m_selection)) {
            const Item *item = m_setup.catalogue->find(id);
            if (!item || drop.contains(id))
                continue;
            for (const QString &dependency : m_setup.catalogue->dependencies(*item))
                needed = drop.remove(dependency) || needed;
        }
    }
    m_selection.subtract(drop);
    for (const QString &id : std::as_const(m_saved->carried.keptItems)) {
        ItemRow *row = m_rows.value(id);
        if (row && row->reason() == tr("You had this before."))
            row->setReason({});
    }
    m_saved.reset();
    refreshRows({}, {}, {}, true);
}

void Wizard::addCarriedRow(ItemRow *row)
{
    m_rows.insert(row->id(), row);
    connect(row, &ItemRow::toggledByUser, this, &Wizard::userToggled);
}

void Wizard::startCarried()
{
    for (const std::unique_ptr<Item> &item : m_saved->carried.owned) {
        const QString id = item->id();
        QString off;
        for (const ZypperSource &source : std::as_const(m_saved->manifest.zypperSources)) {
            if (kCarriedSourcePrefix + source.alias == id)
                off = sourceStartsOffWhy(source, m_saved->manifest.systemId, m_setup.system.id);
        }
        if (!off.isEmpty()) {
            m_rows.value(id)->setReason(off);
            continue;
        }
        const CheckState state = m_results.value(id).state;
        if (state != CheckState::NotDone && state != CheckState::CouldNotTell)
            continue;
        switchOnSaved(id);
    }
}

void Wizard::switchOnKept()
{
    if (!m_saved || m_saved->keptSwitched)
        return;
    m_saved->keptSwitched = true;
    for (const QString &id : std::as_const(m_saved->carried.keptItems)) {
        if (m_results.value(id).state != CheckState::NotDone)
            continue;
        // One that accepts a licence waits for the user's own say (INV-1).
        if (catalogue().find(id)->acceptsLicence()) {
            if (ItemRow *row = m_rows.value(id))
                row->setReason(tr("You had this before."));
            continue;
        }
        switchOnSaved(id);
    }
}

void Wizard::switchOnSaved(const QString &id)
{
    const Selection before = m_selection;
    QStringList included;
    const QStringList pulled = catalogue().switchOn(m_selection, id, m_results, &included);
    m_saved->switchedOn.unite(m_selection - before);
    refreshRows(pulled, included, id, true);
}

QString Wizard::restoreChoicesPath() const { return m_setup.stateDir + QStringLiteral("/restore-choices.json"); }

bool Wizard::writeRestoreChoices() const
{
    Manifest kept;
    for (const QString &id : std::as_const(m_saved->carried.keptItems)) {
        if (m_selection.contains(id))
            kept.items << id;
    }
    for (const SavedSetup::Entry &entry : std::as_const(m_saved->entries)) {
        if (m_selection.contains(entry.id) && (!entry.on || entry.on->isChecked()))
            entry.keep(kept);
    }
    (void)QDir().mkpath(m_setup.stateDir);
    QSaveFile file(restoreChoicesPath());
    const QJsonObject choices{{"file", m_saved->file}, {"manifest", manifestJson(kept)}};
    return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(choices).toJson()) >= 0 && file.commit();
}

QStringList Wizard::secretsArguments(bool remove) const
{
    return m_setup.workerPrefix
           + QStringList{QStringLiteral("--worker"),
                         remove ? QStringLiteral("--remove-secrets") : QStringLiteral("--delete-setup-file"),
                         m_saved->file, QStringLiteral("--lang"), m_setup.language};
}

bool Wizard::stopForClose()
{
    if (!m_runPage->isRunning() && !(m_prepareRunPage && m_prepareRunPage->isRunning())
        && !(m_carryRunPage && m_carryRunPage->isRunning()))
        return false;
    // Ask the Worker to stop between items, and keep this process until it
    // has, so neither Qt nor the AppImage's mount ends it early (design,
    // Stopping).
    (void)QDir().mkpath(m_setup.stateDir);
    QFile stop(stopFilePath(m_setup.stateDir));
    if (!stop.open(QIODevice::WriteOnly)) {
        // The Worker is never told, so it runs on; the window stays and says so.
        auto *box = new QMessageBox(
            QMessageBox::Warning, windowTitle(),
            tr("Groundwork could not ask the run to stop (%1), so it goes on to its end.").arg(stop.errorString()),
            QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
        return true;
    }
    hide();
    return true;
}

void Wizard::closeEvent(QCloseEvent *event)
{
    if (stopForClose())
        event->ignore();
    else
        QWizard::closeEvent(event);
}

void Wizard::reject()
{
    if (!stopForClose())
        QWizard::reject();
}

} // namespace gw
