// The saved setup page (GRND-0079; docs/specs/GRND-0079-start-from-saved-setup.md
// § 4.7): what the saved file holds, what this version cannot offer, and a
// row per item the file becomes, with a switch per entry inside it.
// Leaving it switches on the Groundwork items the file kept (INV-1).
#pragma once

#include <QWizardPage>

class QVBoxLayout;

namespace gw {

class Wizard;

class SavedSetupPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit SavedSetupPage(Wizard *wizard);
    void initializePage() override;
    bool validatePage() override;

private:
    void makeRows();

    Wizard *m_wizard;
    QVBoxLayout *m_outer;
    QWidget *m_inner = nullptr; // remade for each file read
};

} // namespace gw
