// The carry job's pages (GRND-0078; docs/specs/GRND-0078-saved-setup-file.md
// § 4): what this computer's setup holds, each part with a switch, and
// where to save it. A secret switched on says so beside it (INV-2), and
// Save waits for room in the folder (INV-9).
#pragma once

#include "carry/carry.h"

#include <QWizardPage>

#include <functional>

class QLabel;
class QVBoxLayout;

namespace gw {

class Switch;

// What the carry pages read and choose, shared between them.
struct CarryChoice
{
    std::function<Gathered()> gather;                  // off the window's thread
    std::function<FolderSpace(const QString &)> space; // for the folder chosen
    QList<QPair<QString, QString>> items;              // done items: id and title (spec 4.1)
    Gathered found;
    bool gathered = false;
    Manifest kept;      // what the switches keep
    QString folder;     // where to save
    QString choicesPath; // what the save page writes (spec 4.5)
    QString version;    // this Groundwork's
    SystemIdentity system;

    QString output() const;
};

class GatherPage : public QWizardPage
{
    Q_OBJECT
public:
    GatherPage(CarryChoice *choice, QWidget *parent = nullptr);
    void initializePage() override;
    bool isComplete() const override;
    bool validatePage() override; // records what is kept

private:
    void showFound(const Gathered &found);
    QLabel *heading(const QString &text);
    Switch *addRow(const QString &text, bool on, std::function<void(Manifest &)> keep);

    CarryChoice *m_choice;
    QVBoxLayout *m_rows;
    QLabel *m_status;
    bool m_started = false;
    QList<QPair<Switch *, std::function<void(Manifest &)>>> m_keeps;
};

class SavePage : public QWizardPage
{
    Q_OBJECT
public:
    SavePage(CarryChoice *choice, QWidget *parent = nullptr);
    void initializePage() override;
    bool isComplete() const override;
    bool validatePage() override; // writes the choices file

private:
    void refresh();

    CarryChoice *m_choice;
    QLabel *m_file;
    QLabel *m_folder;
    QLabel *m_sizes;
    QLabel *m_problem;
    QLabel *m_error;
    QString m_problemText;
};

} // namespace gw
