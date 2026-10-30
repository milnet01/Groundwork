#include "githubpages.h"

#include "switch.h"

#include "core/statepaths.h"
#include "core/translations.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <QWizard>

#include <algorithm>
#include <memory>

namespace gw {
namespace {

QLabel *wrapped(QWidget *parent, const QString &text = {})
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

// Reads what the job needs off the window's thread; gh can wait on the
// network. What is found reaches the page only while it exists.
void surveyThen(QWizardPage *page, GithubChoice *choice, std::function<void()> then)
{
    auto found = std::make_shared<GithubSurvey>();
    QThread *thread = QThread::create([survey = choice->survey, found] {
        if (survey)
            *found = survey();
    });
    QObject::connect(thread, &QThread::finished, page, [choice, found, then = std::move(then)] {
        choice->found = *found;
        then();
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

QString sizeText(qint64 bytes) { return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeIecFormat); }

qint64 keptBytes(const QList<Repository> &kept)
{
    qint64 total = 0;
    for (const Repository &repository : kept)
        total += repository.diskKiB * 1024;
    return total;
}

// A page the job cannot come back to past this point says Next, not Commit.
void nextOnCommit(QWizardPage *page)
{
    page->setButtonText(QWizard::CommitButton, QCoreApplication::translate("gw::Wizard", "&Next >"));
}

} // namespace

QStringList GithubChoice::toolsArguments() const
{
    return prefix + QStringList{QStringLiteral("--worker"), QStringLiteral("--github-tools"), QStringLiteral("--lang"), language};
}

QStringList GithubChoice::copyArguments() const
{
    return prefix
           + QStringList{QStringLiteral("--worker"), QStringLiteral("--copy-github"), choicesPath, QStringLiteral("--lang"), language};
}

GithubStartPage::GithubStartPage(GithubChoice *choice, QWidget *parent)
    : QWizardPage(parent), m_choice(choice), m_status(wrapped(this))
{
    setTitle(tr("Copy your GitHub projects"));
    setSubTitle(tr("Sign in to GitHub once, choose which of your projects to keep, and choose a folder."));
    nextOnCommit(this);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_status);
    layout->addStretch();
}

void GithubStartPage::initializePage()
{
    m_ready = false;
    m_status->setText(tr("Looking for git and gh…"));
    surveyThen(this, m_choice, [this] {
        const QStringList missing = m_choice->found.missing;
        // Past the install there is no coming back here (spec 4.2).
        setCommitPage(!missing.isEmpty());
        m_status->setText(missing.isEmpty()
                              ? tr("git and gh are on this computer.")
                              : tr("Not on this computer yet: %1. Next installs it, and asks for your password.")
                                    .arg(missing.join(QStringLiteral(", "))));
        m_ready = true;
        emit completeChanged();
    });
}

GithubInstallPage::GithubInstallPage(GithubChoice *choice, QWidget *parent) : RunPage(parent), m_choice(choice)
{
    setTitle(tr("Installing git and gh"));
    setOffersOneUp(false);
    setCommitPage(true);
    nextOnCommit(this);
    connect(this, &RunPage::runFinished, this, [this] {
        if (!endedOk())
            return;
        surveyThen(this, m_choice, [this] {
            m_ready = true;
            emit completeChanged();
        });
    });
}

void GithubInstallPage::initializePage()
{
    m_ready = false;
    start(m_choice->program, m_choice->toolsArguments(), {{QStringLiteral("github-tools"), title()}});
}

GithubSignInPage::GithubSignInPage(GithubChoice *choice, QWidget *parent)
    : QWizardPage(parent), m_choice(choice), m_start(new QPushButton(tr("Sign in to GitHub"), this)),
      m_status(wrapped(this)), m_code(new QLabel(this)), m_copy(new QPushButton(tr("Copy the code"), this)),
      m_open(new QPushButton(tr("Open the browser"), this))
{
    setTitle(tr("Sign in to GitHub"));
    setSubTitle(tr("GitHub shows a page asking for a one-time code. Groundwork never sees your password."));
    setCommitPage(true);
    nextOnCommit(this);
    m_start->setObjectName(QStringLiteral("signIn"));
    m_status->setObjectName(QStringLiteral("signInStatus"));
    m_code->setObjectName(QStringLiteral("oneTimeCode"));
    m_code->setTextFormat(Qt::PlainText);
    m_code->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    QFont large = m_code->font();
    large.setBold(true);
    large.setPointSizeF(large.pointSizeF() * 2.5);
    m_code->setFont(large);
    m_copy->setObjectName(QStringLiteral("copyCode"));
    m_open->setObjectName(QStringLiteral("openBrowser"));
    for (QWidget *w : {static_cast<QWidget *>(m_code), static_cast<QWidget *>(m_copy), static_cast<QWidget *>(m_open)})
        w->hide();
    connect(m_start, &QPushButton::clicked, this, &GithubSignInPage::start);
    connect(m_copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_code->text()); });
    // gh opened no browser with no terminal (measured 2026-10-30), so this does.
    connect(m_open, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/login/device"))); });

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_copy);
    buttons->addWidget(m_open);
    buttons->addStretch();
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_start, 0, Qt::AlignLeading);
    layout->addWidget(m_code);
    layout->addLayout(buttons);
    layout->addWidget(m_status);
    layout->addStretch();

    m_login.setStandardInputFile(QProcess::nullDevice());
    m_login.setStandardOutputFile(QProcess::nullDevice());
    connect(&m_login, &QProcess::readyReadStandardError, this, &GithubSignInPage::readCode);
    connect(&m_login, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus status) { finished(status == QProcess::NormalExit && code == 0); });
    connect(&m_login, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finished(false);
    });
}

GithubSignInPage::~GithubSignInPage()
{
    m_login.disconnect(this);
    stop();
}

void GithubSignInPage::initializePage()
{
    m_ready = false;
    m_status->clear();
    m_start->setEnabled(true);
}

void GithubSignInPage::cleanupPage() { stop(); }

void GithubSignInPage::start()
{
    m_said.clear();
    m_status->setText(tr("Waiting for GitHub…"));
    m_start->setEnabled(false);
    m_login.start(QStringLiteral("gh"), signInArguments());
}

void GithubSignInPage::stop()
{
    if (!isSigningIn())
        return;
    m_login.terminate();
    (void)m_login.waitForFinished(2000); // gh stores nothing until it succeeds
}

void GithubSignInPage::readCode()
{
    m_said += m_login.readAllStandardError();
    if (!m_code->isHidden())
        return;
    const QString code = oneTimeCode(m_said);
    if (code.isEmpty())
        return;
    m_code->setText(code);
    for (QWidget *w : {static_cast<QWidget *>(m_code), static_cast<QWidget *>(m_copy), static_cast<QWidget *>(m_open)})
        w->show();
    m_status->setText(tr("Open the browser, enter this code, and approve Groundwork's sign-in there. This page moves "
                         "on by itself."));
}

void GithubSignInPage::finished(bool ok)
{
    readCode();
    for (QWidget *w : {static_cast<QWidget *>(m_code), static_cast<QWidget *>(m_copy), static_cast<QWidget *>(m_open)})
        w->hide();
    if (!ok) {
        const QString words = signInWords(m_said);
        m_status->setText(words.isEmpty() ? tr("Signing in did not finish. Press the button to try again.")
                                          : tr("Signing in did not finish. Press the button to try again. GitHub's "
                                               "tool said: %1")
                                                .arg(isolated(words)));
        m_start->setEnabled(true);
        return;
    }
    // A warning, such as a plain-file store, is shown on the next page.
    m_choice->signInNote = signInWords(m_said);
    m_status->setText(tr("Signed in. Reading your projects…"));
    surveyThen(this, m_choice, [this] {
        m_ready = true;
        emit completeChanged();
        if (wizard() && wizard()->currentPage() == this)
            wizard()->next();
    });
}

GithubProjectsPage::GithubProjectsPage(GithubChoice *choice, QWidget *parent)
    : QWizardPage(parent), m_choice(choice), m_rows(nullptr), m_account(wrapped(this)), m_note(wrapped(this)),
      m_allOn(new QPushButton(tr("Switch all on"), this)), m_allOff(new QPushButton(tr("Switch all off"), this))
{
    setTitle(tr("Which projects to copy"));
    setSubTitle(tr("Forks start switched off."));
    m_allOn->setObjectName(QStringLiteral("allOn"));
    m_allOff->setObjectName(QStringLiteral("allOff"));
    connect(m_allOn, &QPushButton::clicked, this, [this] { setAll(true); });
    connect(m_allOff, &QPushButton::clicked, this, [this] { setAll(false); });
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_allOn);
    buttons->addWidget(m_allOff);
    buttons->addStretch();
    auto *inner = new QWidget;
    m_rows = new QVBoxLayout(inner);
    m_rows->addStretch();
    auto *scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(inner);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_account);
    layout->addLayout(buttons);
    layout->addWidget(m_note);
    layout->addWidget(scroll, 1);
}

void GithubProjectsPage::initializePage()
{
    const GithubSurvey &found = m_choice->found;
    m_account->setText(found.account.isEmpty() ? tr("Signed in.") : tr("Signed in as %1.").arg(isolated(found.account)));
    QStringList notes;
    if (!m_choice->signInNote.isEmpty())
        notes << m_choice->signInNote;
    if (found.listDamaged || !found.signedIn)
        notes << tr("Couldn't read the list of your projects from GitHub, so nothing can be copied. Go back or close "
                    "the window, and try again later.");
    else if (found.repositories.isEmpty())
        notes << tr("This account owns no projects.");
    else if (found.repositories.size() >= kGithubListLimit)
        notes << tr("Only the first 1000 are shown.");
    m_note->setText(notes.join(QLatin1Char('\n')));
    m_note->setVisible(!notes.isEmpty());

    // Read again with the same list, the switches keep what the user chose.
    QStringList names;
    for (const Repository &repository : found.repositories)
        names << repository.nameWithOwner;
    if (names != m_shown) {
        m_shown = names;
        for (const auto &[row, repository] : std::as_const(m_switches))
            delete row;
        m_switches.clear();
        for (const Repository &repository : found.repositories) {
            const QString size = sizeText(repository.diskKiB * 1024);
            auto *row = new Switch(repository.isPrivate
                                       ? tr("%1 — Private — %2").arg(isolated(repository.name()), size)
                                       : QStringLiteral("%1 — %2").arg(isolated(repository.name()), size),
                                   m_rows->parentWidget());
            row->setObjectName(QStringLiteral("repo:") + repository.nameWithOwner);
            row->setChecked(repository.startsOn());
            connect(row, &QCheckBox::toggled, this, &QWizardPage::completeChanged);
            m_rows->insertWidget(m_rows->count() - 1, row);
            m_switches.append({row, repository});
        }
    }
    m_allOn->setEnabled(!m_switches.isEmpty());
    m_allOff->setEnabled(!m_switches.isEmpty());
}

void GithubProjectsPage::setAll(bool on)
{
    for (const auto &[row, repository] : std::as_const(m_switches))
        row->setChecked(on);
}

bool GithubProjectsPage::isComplete() const
{
    return std::any_of(m_switches.cbegin(), m_switches.cend(), [](const auto &entry) { return entry.first->isChecked(); });
}

bool GithubProjectsPage::validatePage()
{
    m_choice->kept.clear();
    for (const auto &[row, repository] : std::as_const(m_switches)) {
        if (row->isChecked())
            m_choice->kept << repository;
    }
    return true;
}

GithubFolderPage::GithubFolderPage(GithubChoice *choice, QWidget *parent)
    : QWizardPage(parent), m_choice(choice), m_folder(wrapped(this)), m_sizes(wrapped(this)), m_room(wrapped(this)),
      m_problem(wrapped(this)), m_error(wrapped(this))
{
    setTitle(tr("Where to copy them"));
    setSubTitle(tr("Each project gets its own folder inside this one. One already there is left as it is."));
    setCommitPage(true);
    setButtonText(QWizard::CommitButton, tr("Copy"));
    m_room->setObjectName(QStringLiteral("githubRoom"));
    m_problem->setObjectName(QStringLiteral("githubFolderProblem"));
    m_error->hide();
    auto *choose = new QPushButton(tr("Choose a folder…"), this);
    choose->setObjectName(QStringLiteral("chooseGithubFolder"));
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a folder"),
                                                              nearestExistingFolder(m_choice->folder));
        if (dir.isEmpty())
            return;
        m_choice->folder = dir;
        refresh();
    });
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_folder);
    layout->addWidget(choose, 0, Qt::AlignLeading);
    layout->addWidget(m_sizes);
    layout->addWidget(m_room);
    layout->addWidget(m_problem);
    layout->addWidget(m_error);
    layout->addStretch();
}

void GithubFolderPage::initializePage()
{
    if (m_choice->folder.isEmpty())
        m_choice->folder = QDir::homePath() + QStringLiteral("/Projects");
    m_error->hide();
    refresh();
}

void GithubFolderPage::refresh()
{
    m_folder->setText(tr("Folder: %1").arg(isolated(m_choice->folder)));
    const qint64 total = keptBytes(m_choice->kept);
    const FolderSpace space = m_choice->space ? m_choice->space(nearestExistingFolder(m_choice->folder)) : FolderSpace{};
    QString sizes = tr("The projects switched on take about %1.").arg(sizeText(total));
    if (space.freeBytes >= 0)
        sizes += QLatin1Char(' ') + tr("This drive has %1 free.").arg(sizeText(space.freeBytes));
    m_sizes->setText(sizes);
    // GitHub's figure is only an estimate, so this warns and does not stop Next.
    m_room->setText(tr("That may not be enough room. The copy can still be tried."));
    m_room->setVisible(space.freeBytes >= 0 && space.freeBytes < total);
    m_problemText = githubFolderProblem(m_choice->folder);
    m_problem->setText(m_problemText);
    m_problem->setVisible(!m_problemText.isEmpty());
    emit completeChanged();
}

bool GithubFolderPage::validatePage()
{
    GithubChoices choices{m_choice->folder, {}};
    for (const Repository &repository : std::as_const(m_choice->kept))
        choices.repos << repository.nameWithOwner;
    (void)makeStateDir(QFileInfo(m_choice->choicesPath).path());
    QSaveFile file(m_choice->choicesPath);
    const bool saved = file.open(QIODevice::WriteOnly) && file.write(githubChoicesJson(choices)) >= 0 && file.commit();
    if (!saved) {
        m_error->setText(tr("What to copy could not be written to %1, so nothing was copied.")
                             .arg(isolated(m_choice->choicesPath)));
        m_error->show();
    }
    return saved;
}

GithubCopyPage::GithubCopyPage(GithubChoice *choice, QWidget *parent) : RunPage(parent), m_choice(choice)
{
    setTitle(tr("Copying your projects"));
    setOffersOneUp(false);
}

void GithubCopyPage::initializePage()
{
    QList<QPair<QString, QString>> titles;
    for (const Repository &repository : std::as_const(m_choice->kept))
        titles.append({QStringLiteral("github:") + repository.nameWithOwner, repository.name()});
    start(m_choice->program, m_choice->copyArguments(), titles);
}

} // namespace gw
