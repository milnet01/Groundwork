#include "wizard.h"

#include "core/statepaths.h"
#include "core/translations.h"
#include "itemrow.h"
#include "runpage.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLocale>
#include <QScrollArea>
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
            const QString name = code == QLatin1String("en") ? QStringLiteral("English")
                                                             : QLocale(code).nativeLanguageName();
            languages->addItem(name, code);
        }
        languages->setCurrentIndex(qMax(0, languages->findData(language)));
        connect(languages, &QComboBox::currentIndexChanged, this, [this, languages] {
            emit m_wizard->languageChangeRequested(languages->currentData().toString());
        });
        m_status = new QLabel(Wizard::tr("Checking this computer…"), this);
        m_status->setWordWrap(true);
        connect(wizard, &Wizard::checksFinished, this, [this] {
            m_status->setText(Wizard::tr("Checked. Press Next to choose what to set up."));
            emit completeChanged();
        });

        auto *layout = new QVBoxLayout(this);
        layout->addWidget(intro);
        layout->addWidget(languageLabel);
        layout->addWidget(languages);
        layout->addWidget(m_status);
        layout->addStretch();
    }
    bool isComplete() const override { return m_wizard->checksDone(); }

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

} // namespace

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
    connect(m_runPage, &RunPage::runFinished, this, [this] {
        if (!isVisible()) // closed during the run: quit once the Worker is done
            qApp->quit();
    });

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
    refreshRows({}, {}, true);
    m_checksDone = true;
    emit checksFinished();
}

void Wizard::userToggled(const QString &id, bool on)
{
    const QStringList pulled = on ? m_setup.catalogue->switchOn(m_selection, id, m_results)
                                  : m_setup.catalogue->switchOff(m_selection, id);
    refreshRows(pulled, id, on);
}

void Wizard::refreshRows(const QStringList &pulled, const QString &cause, bool on)
{
    for (ItemRow *row : std::as_const(m_rows))
        row->setChecked(m_selection.contains(row->id()));
    const QString causeTitle = cause.isEmpty() ? QString() : m_rows.value(cause)->toggle()->text();
    for (const QString &id : pulled) {
        if (ItemRow *row = m_rows.value(id))
            row->setReason(on ? tr("Switched on because %1 needs it.").arg(causeTitle)
                              : tr("Switched off because it needs %1.").arg(causeTitle));
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
    if (!m_runPage->isRunning())
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
