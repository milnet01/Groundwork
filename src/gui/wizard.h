// The Wizard (GRND-0008; docs/design.md, The levels): a welcome page that
// checks the system, one page per level, a review page, and the run page.
// The prepare job has its own pages after those (GRND-0076).
// It never runs as root and never depends on the Worker: it starts it as a
// child process and stops it by writing the stop file.
#pragma once

#include "carrypages.h"
#include "core/checkrunner.h"
#include "core/selection.h"
#include "preparepages.h"

#include <QFont>
#include <QHash>
#include <QWizard>

#include <functional>
#include <optional>

namespace gw {

class ItemRow;
class RunPage;

// Whether an installed font covers the script of language code, so its
// text shows as letters rather than empty boxes (GRND-0035).
bool fontShowsLanguage(const QString &code);

// Stops Qt thickening a font's own Bold a second time, which smears bold
// Chinese, Japanese and Korean text (GRND-0042). Call before any text is
// drawn.
void useFontsOwnBold();

// The font for language code. For Chinese, Japanese and Korean, the
// installed Noto Sans CJK face for that language comes first, so shared
// characters take that language's shapes (GRND-0044). Otherwise base.
QFont fontFor(const QString &code, const QFont &base);

// The jobs the first page offers (docs/design.md, The shape in one
// paragraph). Each joins WizardSetup::jobs once it is built.
enum class Job { SetUp, CarryOver, PrepareDrive };

struct WizardSetup
{
    const Catalogue *catalogue = nullptr;
    std::function<CheckResults()> runChecks; // run off the window's thread
    QString language;
    QString workerProgram;      // this program, in use
    QStringList workerPrefix;   // arguments before --worker; tests use it
    QString stateDir;
    QList<Job> jobs = {Job::SetUp}; // the jobs built, in the order offered
    bool liveSystem = false;        // nothing set up there lasts a restart
    std::function<DriveSurvey()> surveyDrives; // when the prepare job's drive page opens
    std::function<Gathered()> gatherSetup;     // when the carry job's first page opens
    std::function<FolderSpace(const QString &)> folderSpace;
    SystemIdentity system;                     // recorded in a saved setup
};

class Wizard : public QWizard
{
    Q_OBJECT
public:
    explicit Wizard(WizardSetup setup, QWidget *parent = nullptr);

    bool checksDone() const { return m_checksDone; }
    // The jobs this system is offered: on a live system only preparing a
    // drive, since nothing else there lasts (GRND-0077).
    const QList<Job> &offeredJobs() const { return m_offeredJobs; }
    std::optional<Job> chosenJob() const { return m_chosenJob; }
    void chooseJob(Job job) { m_chosenJob = job; }
    const Selection &selection() const { return m_selection; }
    ItemRow *row(const QString &id) const { return m_rows.value(id); }
    RunPage *runPage() const { return m_runPage; }
    RunPage *prepareRunPage() const { return m_prepareRunPage; }
    RunPage *carryRunPage() const { return m_carryRunPage; }
    QStringList workerArguments() const;
    // The Worker's arguments for preparing the drive (spec 4.4).
    QStringList prepareArguments() const;
    // The Worker's arguments for saving the setup (GRND-0078 spec 4.5).
    QStringList carryArguments() const;
    const CarryChoice &carryChoice() const { return m_carry; }
    const PrepareChoice &prepareChoice() const { return m_prepare; }
    // Routes each job through its own pages.
    int nextId() const override;
    QStringList runOrder() const { return m_setup.catalogue->runOrder(m_selection); }
    // Every switched-on item that takes a value has a valid one.
    bool valuesValid() const;

    // The user switched an item on or off in a row.
    void userToggled(const QString &id, bool on);

signals:
    void languageChangeRequested(const QString &code);
    void checksFinished();

protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;

private:
    void showResults(const CheckResults &results);
    void refreshRows(const QStringList &pulled, const QStringList &included, const QString &cause, bool on);
    void fitRows();
    bool stopForClose();

    WizardSetup m_setup;
    QList<Job> m_offeredJobs;
    std::optional<Job> m_chosenJob;
    CheckResults m_results;
    bool m_checksDone = false;
    Selection m_selection;
    QHash<QString, ItemRow *> m_rows;
    // Each shown reason: the item it names, whether the row was switched on,
    // and whether that item was. It holds only while both still are
    // (GRND-0047).
    struct Reason
    {
        QString cause;
        bool rowOn;
        bool causeOn;
    };
    QHash<QString, Reason> m_reasons;
    QHash<int, QWidget *> m_levelPages; // each level page's row area, keyed by Level
    RunPage *m_runPage;
    PrepareChoice m_prepare;
    int m_drivePageId = -1, m_layoutPageId = -1, m_windowsPageId = -1;
    RunPage *m_prepareRunPage = nullptr;
    CarryChoice m_carry;
    int m_gatherPageId = -1;
    RunPage *m_carryRunPage = nullptr;
};

} // namespace gw
