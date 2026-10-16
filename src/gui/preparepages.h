// The prepare job's pages (GRND-0076; docs/specs/GRND-0076-prepare-a-drive.md
// § 4): choosing the drive, the Windows checklist, the layout, and the
// confirm page whose Apply waits for the "I understand" switch (INV-2).
#pragma once

#include "drives/drives.h"

#include <QWizardPage>

#include <functional>
#include <optional>

class QButtonGroup;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QVBoxLayout;

namespace gw {

class Switch;

// What the prepare pages read and choose, shared between them.
struct PrepareChoice
{
    std::function<DriveSurvey()> survey;
    DriveSurvey found;
    std::optional<Drive> drive; // the drive chosen
    Layout layout;              // the layout chosen
    QString layoutPath;         // where the confirm page writes it (spec 4.4)
};

class DrivePage : public QWizardPage
{
    Q_OBJECT
public:
    DrivePage(PrepareChoice *choice, QWidget *parent = nullptr);
    void initializePage() override;
    bool isComplete() const override;

private:
    void lookAgain();

    PrepareChoice *m_choice;
    QWidget *m_list;
    QButtonGroup *m_group;
};

class WindowsPage : public QWizardPage
{
    Q_OBJECT
public:
    WindowsPage(PrepareChoice *choice, QWidget *parent = nullptr);
    void initializePage() override;

private:
    PrepareChoice *m_choice;
    QLabel *m_steps;
};

// A custom layout: a list of partitions, each with a use and a size in GiB
// or "the rest" (spec 4.3).
class CustomLayoutEditor : public QWidget
{
    Q_OBJECT
public:
    explicit CustomLayoutEditor(QWidget *parent = nullptr);
    void setPartitions(const QList<PlannedPartition> &partitions);
    QList<PlannedPartition> partitions() const;
    void addPartition(const PlannedPartition &partition);
    int count() const { return int(m_rows.size()); }
    void removePartition(int index);

signals:
    void changed();

private:
    struct Row
    {
        QWidget *widget;
        QComboBox *use;
        QDoubleSpinBox *size;
        Switch *rest;
    };
    QVBoxLayout *m_rowsLayout;
    QList<Row> m_rows;
};

class LayoutPage : public QWizardPage
{
    Q_OBJECT
public:
    LayoutPage(PrepareChoice *choice, QWidget *parent = nullptr);
    void initializePage() override;
    bool isComplete() const override;

private:
    qint64 hibernateSwap() const;
    LayoutKind chosenKind() const;
    void rebuildChoices();
    void choose(std::optional<Suggestion> suggestion);
    void refresh();

    PrepareChoice *m_choice;
    std::optional<Suggestion> m_suggestion; // none: the custom layout
    bool m_nothingChosen = false;           // the user must pick (GRND-0084)
    bool m_hibernateTouched = false;
    QWidget *m_choices;
    QButtonGroup *m_group;
    QLabel *m_windowsNote;
    Switch *m_hibernate;
    QLabel *m_hibernateNote;
    QLabel *m_memoryNote;
    CustomLayoutEditor *m_custom;
    QLabel *m_plan;
    QLabel *m_problem;
};

class ConfirmPage : public QWizardPage
{
    Q_OBJECT
public:
    ConfirmPage(PrepareChoice *choice, QWidget *parent = nullptr);
    void initializePage() override;
    bool isComplete() const override;
    bool validatePage() override; // writes the layout file

private:
    PrepareChoice *m_choice;
    QLabel *m_drive;
    QLabel *m_loss;
    QLabel *m_plan;
    Switch *m_understand;
    QLabel *m_error;
};

} // namespace gw
