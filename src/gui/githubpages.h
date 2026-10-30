// The GitHub job's pages (GRND-0065; docs/specs/GRND-0065-copy-github-projects.md
// § 4.2): whether git and gh are here, installing them, signing in, which
// projects, which folder, and the copy. Signing in is the one command that
// changes anything the Wizard starts itself (docs/design.md, What may depend
// on what): it must be cancellable while it waits.
#pragma once

#include "carry/carry.h"
#include "github/github.h"
#include "runpage.h"

#include <QProcess>
#include <QWizardPage>

#include <functional>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace gw {

class Switch;

// What the pages read and choose, shared between them.
struct GithubChoice
{
    std::function<GithubSurvey()> survey;              // off the window's thread
    std::function<FolderSpace(const QString &)> space; // for the folder chosen
    GithubSurvey found;
    QString signInNote;    // what gh warned of while signing in
    QList<Repository> kept; // what the switches keep
    QString folder;        // where to copy to
    QString choicesPath;   // what the folder page writes (spec 4.5)
    QString program;       // the Worker
    QStringList prefix;    // arguments before --worker; tests use it
    QString language;

    QStringList toolsArguments() const;
    QStringList copyArguments() const;
};

class GithubStartPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit GithubStartPage(GithubChoice *choice, QWidget *parent = nullptr);
    void initializePage() override;
    bool isComplete() const override { return m_ready; }

private:
    GithubChoice *m_choice;
    QLabel *m_status;
    bool m_ready = false;
};

class GithubInstallPage : public RunPage
{
    Q_OBJECT
public:
    explicit GithubInstallPage(GithubChoice *choice, QWidget *parent = nullptr);
    void initializePage() override;
    // Only a run that ended ok leads on, once the sign-in is read again.
    bool isComplete() const override { return RunPage::isComplete() && m_ready; }

private:
    GithubChoice *m_choice;
    bool m_ready = false;
};

class GithubSignInPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit GithubSignInPage(GithubChoice *choice, QWidget *parent = nullptr);
    ~GithubSignInPage() override;
    void initializePage() override;
    void cleanupPage() override;
    bool isComplete() const override { return m_ready; }
    bool isSigningIn() const { return m_login.state() != QProcess::NotRunning; }

private:
    void start();
    void stop();
    void readCode();
    void finished(bool ok);

    GithubChoice *m_choice;
    QProcess m_login;
    QByteArray m_said; // what gh wrote to standard error; never logged (INV-1)
    QPushButton *m_start;
    QLabel *m_status;
    QLabel *m_code;
    QPushButton *m_copy;
    QPushButton *m_open;
    bool m_ready = false;
};

class GithubProjectsPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit GithubProjectsPage(GithubChoice *choice, QWidget *parent = nullptr);
    void initializePage() override;
    bool isComplete() const override;
    bool validatePage() override; // records what is kept

private:
    void setAll(bool on);

    GithubChoice *m_choice;
    QVBoxLayout *m_rows;
    QLabel *m_account;
    QLabel *m_note;
    QPushButton *m_allOn;
    QPushButton *m_allOff;
    QStringList m_shown; // the names the rows were made for
    QList<QPair<Switch *, Repository>> m_switches;
};

class GithubFolderPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit GithubFolderPage(GithubChoice *choice, QWidget *parent = nullptr);
    void initializePage() override;
    bool isComplete() const override { return m_problemText.isEmpty(); }
    bool validatePage() override; // writes the choices file

private:
    void refresh();

    GithubChoice *m_choice;
    QLabel *m_folder;
    QLabel *m_sizes;
    QLabel *m_room;
    QLabel *m_problem;
    QLabel *m_error;
    QString m_problemText;
};

class GithubCopyPage : public RunPage
{
    Q_OBJECT
public:
    explicit GithubCopyPage(GithubChoice *choice, QWidget *parent = nullptr);
    void initializePage() override;

private:
    GithubChoice *m_choice;
};

} // namespace gw
