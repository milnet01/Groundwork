#include "wizard.h"

#include "core/statepaths.h"
#include "core/translations.h"
#include "itemrow.h"
#include "runpage.h"
#include "themes.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QHash>
#include <QLabel>
#include <QLocale>
#include <QRadioButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QStyle>
#include <QThread>
#include <QVBoxLayout>

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
                    emit completeChanged();
                });
            }
        }
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
        return ((m_wizard->chosenJob() == Job::SetUp || m_wizard->chosenJob() == Job::CarryOver)
                && m_wizard->checksDone())
               || m_wizard->chosenJob() == Job::PrepareDrive;
    }

private:
    Wizard *m_wizard;
    QLabel *m_status;
};

class ReviewPage : public QWizardPage
{
public:
    ReviewPage(Wizard *wizard, const Catalogue &catalogue)
        : QWizardPage(wizard), m_wizard(wizard), m_catalogue(catalogue), m_list(new QLabel(this))
    {
        setTitle(Wizard::tr("Ready to apply"));
        setCommitPage(true);
        setButtonText(QWizard::CommitButton, Wizard::tr("Apply"));
        m_list->setWordWrap(true);
        m_list->setTextFormat(Qt::PlainText);
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(m_list);
        layout->addStretch();
    }
    void initializePage() override
    {
        const QStringList order = m_catalogue.runOrder(m_wizard->selection());
        if (order.isEmpty()) {
            setSubTitle(Wizard::tr("Nothing is switched on. Go back to choose what to set up."));
            m_list->clear();
            return;
        }
        setSubTitle(Wizard::tr("These run in this order after one password."));
        QStringList lines;
        for (const QString &id : order) {
            const Item *item = m_catalogue.find(id);
            lines << QStringLiteral("• ") + item->title() + QStringLiteral(" — ") + item->applySentence();
        }
        m_list->setText(lines.join(QLatin1Char('\n')));
    }
    // Apply is unavailable until an item is switched on (design, Worker's interface).
    bool isComplete() const override { return !m_wizard->selection().isEmpty() && m_wizard->valuesValid(); }

private:
    Wizard *m_wizard;
    const Catalogue &m_catalogue;
    QLabel *m_list;
};

class RunWizardPage : public RunPage
{
public:
    explicit RunWizardPage(Wizard *wizard) : RunPage(wizard), m_wizard(wizard) {}
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
    Wizard *m_wizard;
    QString m_program;
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
        bool any = false;
        for (const Item *item : m_setup.catalogue->items())
            any = any || item->level() == level;
        if (!any)
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
        addPage(page);
        m_levelPages.insert(int(level), inner);
    }
    addPage(new ReviewPage(this, *m_setup.catalogue));
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
    QThread *thread = QThread::create([this] {
        const CheckResults results = m_setup.runChecks();
        QMetaObject::invokeMethod(this, [this, results] { showResults(results); }, Qt::QueuedConnection);
    });
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
    QList<QWidget *> fitted;
    if (m_levelPages.values().contains(shown->widget()))
        fitted = m_levelPages.values();
    else if (shown->objectName() == QLatin1String("preparePage") || shown->objectName() == QLatin1String("carryPage"))
        fitted = {shown->widget()};
    else
        return;
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
    QStringList included;
    const QStringList pulled = on ? m_setup.catalogue->switchOn(m_selection, id, m_results, &included)
                                  : m_setup.catalogue->switchOff(m_selection, id);
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
}

QStringList Wizard::workerArguments() const
{
    QStringList args = m_setup.workerPrefix
        + QStringList{QStringLiteral("--worker"), QStringLiteral("--lang"), m_setup.language};
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
        if (row && row->valueField() && !m_setup.catalogue->find(id)->isValidValue(row->value()))
            return false;
    }
    return true;
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
    (void)stop.open(QIODevice::WriteOnly);
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
