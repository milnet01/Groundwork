#include "carrypages.h"

#include "switch.h"

#include "core/statepaths.h"
#include "core/translations.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSysInfo>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

#include <pwd.h>
#include <unistd.h>

namespace gw {
namespace {

QString bytesText(qint64 bytes)
{
    if (bytes < 0)
        return QCoreApplication::translate("gw::Carry", "size unknown");
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeIecFormat);
}

QLabel *wrapped(QWidget *parent, const QString &text = {})
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

// The page's content scrolls; the wizard grows the window to show it where
// the screen allows (Wizard::fitRows).
QVBoxLayout *scrolled(QWizardPage *page)
{
    auto *inner = new QWidget;
    auto *layout = new QVBoxLayout(inner);
    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("carryPage"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(inner);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
    return layout;
}

qint64 settingsBytes(const Manifest &manifest)
{
    // -1 when a size is unknown (spec 4.4).
    qint64 total = 0;
    for (const SettingsRow &row : manifest.settings) {
        if (row.bytes < 0)
            return -1;
        total += row.bytes;
    }
    return total;
}

} // namespace

QString usualSetupFolder()
{
    // The account's own name, since USER may be unset and /run/media/ alone
    // lists every account's folder.
    const passwd *account = getpwuid(geteuid());
    if (!account || !account->pw_name || !*account->pw_name)
        return QDir::homePath();
    const QDir media(QStringLiteral("/run/media/") + QString::fromLocal8Bit(account->pw_name));
    const QStringList sticks = media.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    return sticks.isEmpty() ? QDir::homePath() : media.filePath(sticks.first());
}

QString CarryChoice::output() const
{
    return QDir(folder).filePath(setupFileName(QSysInfo::machineHostName(), QDate::currentDate()));
}

GatherPage::GatherPage(CarryChoice *choice, QWidget *parent)
    : QWizardPage(parent), m_choice(choice), m_status(nullptr)
{
    setTitle(tr("What to carry over"));
    setSubTitle(tr("Groundwork found these on this computer. Switch off anything you do not want on the new "
                   "install."));
    m_rows = scrolled(this);
    m_status = wrapped(this, tr("Looking at this computer's setup…"));
    m_rows->addWidget(m_status);
    m_rows->addStretch();
}

void GatherPage::initializePage()
{
    if (m_started)
        return; // gathered once; the switches keep what the user chose
    m_started = true;
    // du over the home folder can take a while, so off the window's thread.
    // What is found reaches this page only while it exists: changing the
    // language replaces the window while gathering may still run.
    auto found = std::make_shared<Gathered>();
    QThread *thread = QThread::create([gather = m_choice->gather, found] {
        if (gather)
            *found = gather();
    });
    connect(thread, &QThread::finished, this, [this, found] { showFound(*found); });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

bool GatherPage::isComplete() const { return m_choice->gathered; }

QLabel *GatherPage::heading(const QString &text)
{
    auto *label = wrapped(m_status->parentWidget(), text);
    QFont bold = label->font();
    bold.setBold(true);
    label->setFont(bold);
    m_rows->insertWidget(m_rows->count() - 1, label);
    return label;
}

Switch *GatherPage::addRow(const QString &text, bool on, std::function<void(Manifest &)> keep)
{
    auto *row = new Switch(text, m_status->parentWidget());
    row->setChecked(on);
    m_rows->insertWidget(m_rows->count() - 1, row);
    if (keep)
        m_keeps.append({row, std::move(keep)});
    return row;
}

void GatherPage::showFound(const Gathered &found)
{
    m_choice->found = found;
    m_status->hide();
    QWidget *area = m_status->parentWidget();
    auto note = [&](const QString &text) { m_rows->insertWidget(m_rows->count() - 1, wrapped(area, text)); };
    auto couldNotTell = [&](const QString &why) { note(tr("Couldn't tell: %1").arg(why)); };

    heading(tr("Groundwork's items that are done"));
    if (m_choice->items.isEmpty())
        note(tr("None are done yet."));
    for (const auto &[id, title] : std::as_const(m_choice->items))
        addRow(title, true, [id](Manifest &m) { m.items << id; });

    heading(tr("Programs you asked for"));
    if (!found.packagesProblem.isEmpty())
        couldNotTell(found.packagesProblem);
    if (!found.flatpaksProblem.isEmpty())
        couldNotTell(found.flatpaksProblem);
    for (const QString &name : found.packages)
        addRow(isolated(name), true, [name](Manifest &m) { m.packages << name; });
    for (const FlatpakApp &app : found.flatpaks)
        addRow(tr("%1 (Flatpak)").arg(isolated(app.app)), true, [app](Manifest &m) { m.flatpaks << app; });

    heading(tr("Software sources you added"));
    if (!found.remotesProblem.isEmpty())
        couldNotTell(found.remotesProblem);
    for (const QString &file : found.unreadableRepos) {
        Switch *row = addRow(tr("Couldn't tell: %1 could not be read.").arg(isolated(file)), false, {});
        row->setEnabled(false);
    }
    for (const QString &why : found.uncarriedSources)
        addRow(why, false, {})->setEnabled(false);
    if (found.zypperSources.isEmpty() && found.flatpakRemotes.isEmpty() && found.unreadableRepos.isEmpty()
        && found.uncarriedSources.isEmpty())
        note(tr("None found."));
    for (const ZypperSource &s : found.zypperSources)
        addRow(QStringLiteral("%1 — %2").arg(isolated(s.name), isolated(s.baseurl)), true,
               [s](Manifest &m) { m.zypperSources << s; });
    for (const FlatpakRemote &r : found.flatpakRemotes)
        addRow(tr("%1 (Flatpak) — %2").arg(isolated(r.name), isolated(r.url)), true,
               [r](Manifest &m) { m.flatpakRemotes << r; });

    heading(tr("Settings, app by app"));
    note(tr("Each starts switched on, except settings that hold saved passwords or keys, and settings bigger "
            "than 100 MiB."));
    for (const SettingsRow &setting : found.settings) {
        Switch *row = addRow(QStringLiteral("%1 — %2").arg(isolated(setting.name), bytesText(setting.bytes)),
                             startsOn(setting), [setting](Manifest &m) { m.settings << setting; });
        row->setObjectName(QStringLiteral("setting:") + setting.name);
        if (!setting.secret)
            continue;
        // Switching a secret on says what that means, beside it (INV-2).
        QLabel *warning = wrapped(area, tr("The saved file will hold this app's saved passwords or keys. Anyone "
                                           "with the file can read them."));
        warning->setObjectName(QStringLiteral("secretWarning:") + setting.name);
        warning->setVisible(row->isChecked());
        m_rows->insertWidget(m_rows->count() - 1, warning);
        connect(row, &QCheckBox::toggled, warning, &QWidget::setVisible);
    }
    m_choice->gathered = true;
    emit completeChanged();
}

bool GatherPage::validatePage()
{
    Manifest kept;
    for (const auto &[row, keep] : std::as_const(m_keeps)) {
        if (row->isChecked())
            keep(kept);
    }
    m_choice->kept = std::move(kept);
    return true;
}

SavePage::SavePage(CarryChoice *choice, QWidget *parent)
    : QWizardPage(parent), m_choice(choice), m_file(wrapped(this)), m_folder(wrapped(this)), m_sizes(wrapped(this)),
      m_problem(wrapped(this)), m_error(wrapped(this))
{
    setTitle(tr("Save the setup"));
    setSubTitle(tr("Groundwork saves it as one file. A USB stick is the usual place."));
    setCommitPage(true);
    setButtonText(QWizard::CommitButton, tr("Save"));
    m_problem->setObjectName(QStringLiteral("saveProblem"));
    m_error->hide();
    auto *choose = new QPushButton(tr("Choose a folder…"), this);
    choose->setObjectName(QStringLiteral("chooseFolder"));
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a folder"), m_choice->folder);
        if (dir.isEmpty())
            return;
        m_choice->folder = dir;
        refresh();
    });
    auto *layout = scrolled(this);
    layout->addWidget(m_file);
    layout->addWidget(m_folder);
    layout->addWidget(choose, 0, Qt::AlignLeading);
    layout->addWidget(m_sizes);
    layout->addWidget(m_problem);
    layout->addWidget(wrapped(this, tr("The file holds the settings as they are now. Saved passwords in an app "
                                       "Groundwork does not know are not marked, so switch off any app that keeps "
                                       "passwords unless you mean to carry them.")));
    layout->addWidget(m_error);
    layout->addStretch();
}

void SavePage::initializePage()
{
    if (m_choice->folder.isEmpty())
        m_choice->folder = usualSetupFolder();
    m_error->hide();
    refresh();
}

void SavePage::refresh()
{
    m_file->setText(tr("File: %1").arg(isolated(QFileInfo(m_choice->output()).fileName())));
    m_folder->setText(tr("Folder: %1").arg(isolated(m_choice->folder)));
    const FolderSpace space = m_choice->space ? m_choice->space(m_choice->folder) : FolderSpace{};
    const qint64 total = settingsBytes(m_choice->kept);
    QString sizes = tr("The settings switched on take %1.").arg(bytesText(total));
    if (total < 0)
        sizes = tr("The size of some settings switched on is unknown.");
    if (space.freeBytes >= 0)
        sizes += QLatin1Char(' ') + tr("This folder has %1 free, on a %2 drive.").arg(bytesText(space.freeBytes),
                                                                                      isolated(space.fstype));
    m_sizes->setText(sizes);

    // Save waits for room, saying which rows are largest (spec 4.5, INV-9).
    m_problemText = saveProblem(total, space.freeBytes, space.fstype);
    QString problem = m_problemText;
    if (!problem.isEmpty() && !m_choice->kept.settings.isEmpty()) {
        QList<SettingsRow> largest = m_choice->kept.settings;
        std::sort(largest.begin(), largest.end(), [](const SettingsRow &a, const SettingsRow &b) { return a.bytes > b.bytes; });
        QStringList names;
        for (const QList<SettingsRow> topRows = largest.mid(0, 3); const SettingsRow &row : topRows)
            names << QStringLiteral("%1 (%2)").arg(isolated(row.name), bytesText(row.bytes));
        problem += QLatin1Char('\n') + tr("The largest: %1.").arg(names.join(QStringLiteral(", ")));
    }
    m_problem->setText(problem);
    m_problem->setVisible(!problem.isEmpty());
    emit completeChanged();
}

bool SavePage::isComplete() const { return m_problemText.isEmpty(); }

bool SavePage::validatePage()
{
    Manifest manifest = m_choice->kept;
    manifest.created = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    manifest.groundwork = m_choice->version;
    manifest.systemId = m_choice->system.id;
    manifest.systemVersion = m_choice->system.versionId;
    (void)makeStateDir(QFileInfo(m_choice->choicesPath).path());
    QSaveFile file(m_choice->choicesPath);
    const QJsonObject choices{{"output", m_choice->output()}, {"manifest", manifestJson(manifest)}};
    const bool saved = file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(choices).toJson()) >= 0
                       && file.commit();
    if (!saved) {
        m_error->setText(tr("What to save could not be written to %1, so nothing was saved.")
                             .arg(isolated(m_choice->choicesPath)));
        m_error->show();
    }
    return saved;
}

} // namespace gw
