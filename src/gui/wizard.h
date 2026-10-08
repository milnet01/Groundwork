// The Wizard (GRND-0008; docs/design.md, The levels): a welcome page that
// checks the system, one page per level, a review page, and the run page.
// It never runs as root and never depends on the Worker: it starts it as a
// child process and stops it by writing the stop file.
#pragma once

#include "core/checkrunner.h"
#include "core/selection.h"

#include <QHash>
#include <QWizard>

#include <functional>

namespace gw {

class ItemRow;
class RunPage;

// Whether an installed font covers the script of language code, so its
// text shows as letters rather than empty boxes (GRND-0035).
bool fontShowsLanguage(const QString &code);

struct WizardSetup
{
    const Catalogue *catalogue = nullptr;
    std::function<CheckResults()> runChecks; // run off the window's thread
    QString language;
    QString workerProgram;      // this program, in use
    QStringList workerPrefix;   // arguments before --worker; tests use it
    QString stateDir;
};

class Wizard : public QWizard
{
    Q_OBJECT
public:
    explicit Wizard(WizardSetup setup, QWidget *parent = nullptr);

    bool checksDone() const { return m_checksDone; }
    const Selection &selection() const { return m_selection; }
    ItemRow *row(const QString &id) const { return m_rows.value(id); }
    RunPage *runPage() const { return m_runPage; }
    QStringList workerArguments() const;
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
    void refreshRows(const QStringList &pulled, const QString &cause, bool on);
    bool stopForClose();

    WizardSetup m_setup;
    CheckResults m_results;
    bool m_checksDone = false;
    Selection m_selection;
    QHash<QString, ItemRow *> m_rows;
    QHash<int, QWidget *> m_levelPages; // each level page's row area, keyed by Level
    RunPage *m_runPage;
};

} // namespace gw
