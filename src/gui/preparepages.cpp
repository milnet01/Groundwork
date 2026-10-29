#include "preparepages.h"

#include "switch.h"

#include "core/translations.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

namespace gw {
namespace {

// Drives are sold in GB, so they are shown in GB; planned partitions are
// sized in GiB, as the custom layout's sizes are typed (spec 4.1, 4.3).
QString gb(qint64 bytes) { return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat); }
QString gib(qint64 bytes) { return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeIecFormat); }

QString connection(const QString &tran)
{
    if (tran == QLatin1String("usb"))
        return QStringLiteral("USB");
    if (tran == QLatin1String("nvme"))
        return QStringLiteral("NVMe");
    return tran.toUpper();
}

// Each line runs the window's way, whatever text it starts with.
QString lineMark() { return QGuiApplication::isRightToLeft() ? QString(QChar(0x200F)) : QString(); }

QString driveTitle(const Drive &drive)
{
    const QString model = drive.model.isEmpty()
                              ? QCoreApplication::translate("gw::PreparePages", "Unnamed drive")
                              : isolated(drive.model);
    QStringList facts{gb(drive.size)};
    if (!drive.tran.isEmpty())
        facts << isolated(connection(drive.tran));
    facts << isolated(drive.path);
    return lineMark() + QStringLiteral("%1 — %2").arg(model, facts.join(QStringLiteral(", ")));
}

// One line per partition the drive holds now: size, filesystem and label.
QString partitionLines(const Drive &drive)
{
    if (drive.partitions.isEmpty())
        return QCoreApplication::translate("gw::PreparePages", "No partitions.");
    QStringList lines;
    for (const Partition &p : drive.partitions) {
        QStringList facts{gb(p.size)};
        if (!p.fstype.isEmpty())
            facts << isolated(p.fstype);
        if (!p.label.isEmpty())
            facts << QStringLiteral("“%1”").arg(isolated(p.label));
        lines << lineMark() + QStringLiteral("• %1 — %2").arg(isolated(p.path), facts.join(QStringLiteral(", ")));
    }
    return lines.join(QLatin1Char('\n'));
}

// One line per planned partition, with the size it will have on this drive.
QString planLines(const Layout &layout, const Drive &drive)
{
    const QList<qint64> sizes = partitionSizes(layout, drive);
    QStringList lines;
    for (qsizetype i = 0; i < layout.partitions.size(); ++i)
        lines << QStringLiteral("• %1 — %2").arg(useTitle(layout.partitions[i].use), gib(sizes[i]));
    return lines.join(QLatin1Char('\n'));
}

QLabel *wrapped(QWidget *parent, const QString &text = {})
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}

// A page's content scrolls rather than squeezing its wrapped text; the
// wizard grows the window to show it whole where the screen allows
// (Wizard::fitRows).
QVBoxLayout *scrolled(QWizardPage *page)
{
    auto *inner = new QWidget;
    auto *layout = new QVBoxLayout(inner);
    auto *scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("preparePage"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(inner);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
    return layout;
}

// Empties a list of choices. A choice may be the one whose signal asked for
// this, so each leaves the page now and is deleted later.
void clear(QWidget *box, QButtonGroup *group)
{
    for (QAbstractButton *button : group->buttons())
        group->removeButton(button);
    for (QWidget *child : box->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
        child->hide();
        child->setParent(nullptr);
        child->deleteLater();
    }
}

} // namespace

DrivePage::DrivePage(PrepareChoice *choice, QWidget *parent)
    : QWizardPage(parent), m_choice(choice), m_list(new QWidget(this)), m_group(new QButtonGroup(this))
{
    setTitle(tr("Choose the drive"));
    setSubTitle(tr("Groundwork prepares one drive. A drive in use cannot be chosen."));
    new QVBoxLayout(m_list);
    auto *again = new QPushButton(tr("Look again"), this);
    again->setObjectName(QStringLiteral("lookAgain"));
    connect(again, &QPushButton::clicked, this, &DrivePage::lookAgain);
    auto *layout = scrolled(this);
    layout->addWidget(m_list);
    layout->addWidget(again, 0, Qt::AlignLeading);
    layout->addStretch();
}

void DrivePage::initializePage() { lookAgain(); }

bool DrivePage::isComplete() const { return m_choice->drive.has_value(); }

void DrivePage::lookAgain()
{
    m_choice->found = m_choice->survey ? m_choice->survey() : DriveSurvey{};
    const QString kept = m_choice->drive ? m_choice->drive->path : QString();
    m_choice->drive.reset();
    clear(m_list, m_group);
    auto *rows = static_cast<QVBoxLayout *>(m_list->layout());
    if (!m_choice->found.problem.isEmpty())
        rows->addWidget(wrapped(m_list, m_choice->found.problem));
    else if (m_choice->found.drives.isEmpty())
        rows->addWidget(wrapped(m_list, tr("No drive was found. Plug one in, then press Look again.")));
    for (const Drive &drive : std::as_const(m_choice->found.drives)) {
        auto *pick = new QRadioButton(driveTitle(drive), m_list);
        pick->setObjectName(drive.path);
        pick->setEnabled(drive.inUse.isEmpty());
        m_group->addButton(pick);
        auto *details = wrapped(m_list, drive.inUse.isEmpty() ? partitionLines(drive)
                                                               : drive.inUse + QLatin1Char('\n') + partitionLines(drive));
        details->setIndent(pick->style()->pixelMetric(QStyle::PM_ExclusiveIndicatorWidth) * 2);
        rows->addWidget(pick);
        rows->addWidget(details);
        connect(pick, &QRadioButton::toggled, this, [this, drive](bool on) {
            if (!on)
                return;
            m_choice->drive = drive;
            emit completeChanged();
        });
        if (drive.path == kept && drive.inUse.isEmpty())
            pick->setChecked(true);
    }
    emit completeChanged();
}

WindowsPage::WindowsPage(PrepareChoice *choice, QWidget *parent)
    : QWizardPage(parent), m_choice(choice), m_steps(wrapped(this))
{
    setObjectName(QStringLiteral("windowsChecklist"));
    setTitle(tr("Windows is on this drive"));
    setSubTitle(tr("Before going on, do these in Windows. They are advice: Continue needs none of them."));
    setButtonText(QWizard::NextButton, tr("Continue"));
    auto *layout = scrolled(this);
    layout->addWidget(m_steps);
    layout->addStretch();
}

void WindowsPage::initializePage()
{
    const Drive &drive = *m_choice->drive;
    QStringList bitlocker;
    for (const Partition &p : drive.partitions) {
        if (p.fstype == QLatin1String("BitLocker"))
            bitlocker << p.path;
    }
    QStringList steps{
        tr("Back up everything on this drive you want to keep."),
        tr("Turn off Fast Startup, in Windows' Power Options. Otherwise Windows leaves its partition "
           "half-closed, and Linux must not touch it."),
        bitlocker.isEmpty()
            ? tr("Check whether BitLocker is on. If it is, write down its recovery key.")
            : tr("BitLocker is on for %1. Write down its recovery key.").arg(bitlocker.join(QStringLiteral(", "))),
        tr("To put openSUSE beside Windows, make room first: shrink Windows in Windows' own Disk "
           "Management. Groundwork never shrinks Windows itself."),
    };
    if (drive.rotational)
        steps << tr("Optional, on a spinning disk like this one: defragment Windows first. It lets Windows "
                    "shrink further; it protects nothing.");
    for (QString &step : steps)
        step.prepend(QStringLiteral("• "));
    m_steps->setText(steps.join(QStringLiteral("\n\n")));
}

CustomLayoutEditor::CustomLayoutEditor(QWidget *parent) : QWidget(parent), m_rowsLayout(new QVBoxLayout)
{
    auto *add = new QPushButton(tr("Add a partition"), this);
    add->setObjectName(QStringLiteral("addPartition"));
    connect(add, &QPushButton::clicked, this, [this] { addPartition({Use::Data, 10 * kGiB}); });
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(m_rowsLayout);
    layout->addWidget(add, 0, Qt::AlignLeading);
}

void CustomLayoutEditor::setPartitions(const QList<PlannedPartition> &partitions)
{
    while (!m_rows.isEmpty())
        removePartition(0);
    for (const PlannedPartition &p : partitions)
        addPartition(p);
}

void CustomLayoutEditor::addPartition(const PlannedPartition &partition)
{
    Row row;
    row.widget = new QWidget(this);
    row.use = new QComboBox(row.widget);
    for (Use use : allUses())
        row.use->addItem(useTitle(use), int(use));
    row.use->setCurrentIndex(row.use->findData(int(partition.use)));
    row.use->setAccessibleName(tr("Use"));
    row.size = new QDoubleSpinBox(row.widget);
    row.size->setRange(0.1, 1024 * 1024);
    row.size->setDecimals(1);
    row.size->setSuffix(QStringLiteral(" GiB"));
    row.size->setAccessibleName(tr("Size"));
    row.size->setValue(double(partition.size.value_or(10 * kGiB)) / kGiB);
    row.rest = new Switch(tr("The rest"), row.widget);
    row.rest->setChecked(!partition.size);
    row.size->setEnabled(partition.size.has_value());
    auto *remove = new QPushButton(tr("Remove"), row.widget);
    auto *line = new QHBoxLayout(row.widget);
    line->setContentsMargins(0, 0, 0, 0);
    line->addWidget(row.use);
    line->addWidget(row.size);
    line->addWidget(row.rest);
    line->addWidget(remove);
    line->addStretch();
    m_rowsLayout->addWidget(row.widget);
    m_rows << row;

    connect(row.use, &QComboBox::currentIndexChanged, this, &CustomLayoutEditor::changed);
    connect(row.size, &QDoubleSpinBox::valueChanged, this, &CustomLayoutEditor::changed);
    connect(row.rest, &QCheckBox::toggled, this, [this, size = row.size](bool on) {
        size->setEnabled(!on);
        emit changed();
    });
    connect(remove, &QPushButton::clicked, this, [this, widget = row.widget] {
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows[i].widget == widget)
                return removePartition(i);
        }
    });
    emit changed();
}

void CustomLayoutEditor::removePartition(int index)
{
    m_rows.takeAt(index).widget->deleteLater();
    emit changed();
}

QList<PlannedPartition> CustomLayoutEditor::partitions() const
{
    QList<PlannedPartition> out;
    for (const Row &row : m_rows) {
        const Use use = Use(row.use->currentData().toInt());
        if (row.rest->isChecked())
            out << PlannedPartition{use, std::nullopt};
        else // typed in GiB; the layout needs whole MiB
            out << PlannedPartition{use, qRound64(row.size->value() * 1024) * kMiB};
    }
    return out;
}

LayoutPage::LayoutPage(PrepareChoice *choice, QWidget *parent)
    : QWizardPage(parent), m_choice(choice), m_choices(new QWidget(this)), m_group(new QButtonGroup(this)),
      m_windowsNote(wrapped(this)), m_hibernate(new Switch(tr("Room to hibernate"), this)),
      m_hibernateNote(wrapped(this)), m_memoryNote(wrapped(this)), m_custom(new CustomLayoutEditor(this)),
      m_plan(wrapped(this)), m_problem(wrapped(this))
{
    setTitle(tr("Choose a layout"));
    setSubTitle(tr("Groundwork suggests the layouts that fit this drive. You can build your own instead."));
    new QVBoxLayout(m_choices);
    m_choices->layout()->setContentsMargins(0, 0, 0, 0);
    m_hibernate->setObjectName(QStringLiteral("hibernate"));
    m_hibernateNote->setObjectName(QStringLiteral("hibernateNote"));
    m_problem->setObjectName(QStringLiteral("layoutProblem"));
    connect(m_hibernate, &QCheckBox::toggled, this, [this] {
        m_hibernateTouched = true;
        rebuildChoices();
    });
    connect(m_custom, &CustomLayoutEditor::changed, this, &LayoutPage::refresh);

    auto *layout = scrolled(this);
    layout->addWidget(m_choices);
    layout->addWidget(m_windowsNote);
    layout->addWidget(m_custom);
    layout->addWidget(m_hibernate);
    layout->addWidget(m_hibernateNote);
    layout->addWidget(m_memoryNote);
    layout->addWidget(m_plan);
    layout->addWidget(m_problem);
    layout->addStretch();
}

qint64 LayoutPage::hibernateSwap() const
{
    return m_hibernate->isChecked() ? m_choice->found.memorySwap : 0;
}

LayoutKind LayoutPage::chosenKind() const
{
    return m_suggestion == Suggestion::Beside ? LayoutKind::Beside : LayoutKind::Whole;
}

void LayoutPage::initializePage()
{
    m_hibernateTouched = false;
    const Drive &drive = *m_choice->drive;
    const QList<Suggestion> offered = suggestionsFor(drive, m_choice->found.firmware, 0);
    m_suggestion = offered.isEmpty() ? std::nullopt : std::optional<Suggestion>(offered.first());
    // With Windows on the drive, start on keeping it (the user, 2026-10-16).
    if (drive.hasWindows() && offered.contains(Suggestion::Beside))
        m_suggestion = Suggestion::Beside;
    m_custom->setPartitions(layoutFor(Suggestion::Whole, m_choice->found.firmware, 0).partitions);
    // Memory unread: no switch, and the swap stays 2 GiB.
    m_hibernate->setVisible(m_choice->found.memorySwap > 0);
    m_memoryNote->setVisible(m_choice->found.memorySwap > 0);
    m_memoryNote->setText(tr("Room to hibernate sizes the swap to this computer's memory, %1. Preparing the "
                             "drive for another computer? Build your own layout, with swap the size of that "
                             "computer's memory.").arg(gib(m_choice->found.memorySwap)));
    choose(m_suggestion);
}

void LayoutPage::choose(std::optional<Suggestion> suggestion)
{
    m_suggestion = suggestion;
    m_nothingChosen = false;
    // Until the user sets it, the switch starts by its share of the space
    // this layout uses (spec 4.3, INV-9).
    if (!m_hibernateTouched && m_suggestion) {
        const QSignalBlocker block(m_hibernate);
        m_hibernate->setChecked(
            hibernateStartsOn(m_choice->found.memorySwap, usableSpace(*m_choice->drive, chosenKind())));
    }
    rebuildChoices();
}

void LayoutPage::rebuildChoices()
{
    const Drive &drive = *m_choice->drive;
    const QList<Suggestion> offered = suggestionsFor(drive, m_choice->found.firmware, hibernateSwap());
    if (m_suggestion && !offered.contains(*m_suggestion)) {
        // With Windows here, the first one left erases it: choose nothing,
        // so the user picks (the user, 2026-10-16).
        m_nothingChosen = drive.hasWindows();
        m_suggestion = offered.isEmpty() || m_nothingChosen ? std::nullopt
                                                            : std::optional<Suggestion>(offered.first());
    }
    clear(m_choices, m_group);
    auto add = [this](const QString &title, const QString &name, std::optional<Suggestion> s) {
        auto *pick = new QRadioButton(title, m_choices);
        pick->setObjectName(name);
        m_group->addButton(pick);
        m_choices->layout()->addWidget(pick);
        pick->setChecked(!m_nothingChosen && m_suggestion == s);
        connect(pick, &QRadioButton::toggled, this, [this, s](bool on) {
            if (on)
                choose(s);
        });
    };
    for (Suggestion s : offered)
        add(suggestionTitle(s), QStringLiteral("suggestion%1").arg(int(s)), s);
    add(tr("Build my own layout"), QStringLiteral("custom"), std::nullopt);

    // Windows here, but no room beside it: say why (spec 6).
    m_windowsNote->hide();
    if (drive.hasWindows() && !offered.contains(Suggestion::Beside)) {
        m_windowsNote->setText(
            drive.pttype != QLatin1String("gpt")
                ? tr("Windows is on this drive, but on an older MBR partition table, so Groundwork cannot put "
                     "openSUSE beside it. The openSUSE installer can.")
                : tr("There is not enough free space beside Windows: openSUSE needs %1. Shrink Windows in its "
                     "own Disk Management first, then come back.")
                      .arg(gb(suggestionMinimum(Suggestion::Beside, hibernateSwap()))));
        m_windowsNote->show();
    }
    refresh();
}

void LayoutPage::refresh()
{
    const Drive &drive = *m_choice->drive;
    const bool custom = !m_suggestion && !m_nothingChosen;
    m_custom->setVisible(custom);
    m_hibernate->setEnabled(!custom); // a custom layout sizes its own swap
    if (m_nothingChosen) {
        m_hibernateNote->hide();
        m_plan->clear();
        m_problem->hide();
        emit completeChanged();
        return;
    }
    const qint64 space = usableSpace(drive, chosenKind());
    const qint64 swap = m_choice->found.memorySwap;
    m_hibernateNote->setVisible(!custom && swap > 0 && !hibernateStartsOn(swap, space) && !m_hibernate->isChecked());
    m_hibernateNote->setText(tr("Room to hibernate takes %1, %2 % of the space this layout uses.")
                                 .arg(gib(swap))
                                 .arg(space > 0 ? qRound(100.0 * double(swap) / double(space)) : 100));

    Layout &layout = m_choice->layout;
    if (custom)
        layout = {LayoutKind::Whole, m_choice->found.firmware, m_custom->partitions()};
    else
        layout = layoutFor(*m_suggestion, m_choice->found.firmware, hibernateSwap());
    const QString problem = validate(layout, drive);
    m_problem->setText(problem);
    m_problem->setVisible(!problem.isEmpty());
    m_plan->setText(problem.isEmpty() ? tr("The drive will hold:") + QLatin1Char('\n') + planLines(layout, drive)
                                      : QString());
    emit completeChanged();
}

bool LayoutPage::isComplete() const
{
    return !m_nothingChosen && m_choice->drive && validate(m_choice->layout, *m_choice->drive).isEmpty();
}

ConfirmPage::ConfirmPage(PrepareChoice *choice, QWidget *parent)
    : QWizardPage(parent), m_choice(choice), m_drive(wrapped(this)), m_loss(wrapped(this)), m_plan(wrapped(this)),
      m_understand(new Switch(tr("I understand"), this)), m_error(wrapped(this))
{
    setTitle(tr("Check, then confirm"));
    setCommitPage(true);
    setButtonText(QWizard::CommitButton, tr("Apply"));
    m_understand->setObjectName(QStringLiteral("understand"));
    connect(m_understand, &QCheckBox::toggled, this, &ConfirmPage::completeChanged);
    m_error->hide();
    auto *layout = scrolled(this);
    layout->addWidget(m_drive);
    layout->addWidget(m_loss);
    layout->addWidget(m_plan);
    layout->addWidget(m_understand);
    layout->addWidget(m_error);
    layout->addStretch();
}

void ConfirmPage::initializePage()
{
    const Drive &drive = *m_choice->drive;
    const Layout &layout = m_choice->layout;
    m_understand->setChecked(false);
    m_error->hide();
    m_drive->setText(driveTitle(drive) + QLatin1Char('\n') + partitionLines(drive));
    if (layout.kind == LayoutKind::Beside) {
        setSubTitle(tr("Nothing already on this drive changes. Press Apply only once you have switched on "
                       "\"I understand\"."));
        m_loss->setText(tr("The partitions above stay as they are. openSUSE's partitions go in the free space."));
    } else {
        setSubTitle(tr("Everything on this drive will be erased, and this cannot be undone. Press Apply only "
                       "once you have switched on \"I understand\"."));
        m_loss->setText(drive.partitions.isEmpty()
                            ? tr("The drive holds no partitions now.")
                            : tr("Every partition above will be erased, with everything on it."));
    }
    m_plan->setText(tr("The drive will then hold:") + QLatin1Char('\n') + planLines(layout, drive));
}

// Apply needs the switch on (INV-2).
bool ConfirmPage::isComplete() const { return m_understand->isChecked(); }

bool ConfirmPage::validatePage()
{
    (void)QDir().mkpath(QFileInfo(m_choice->layoutPath).path());
    QSaveFile file(m_choice->layoutPath);
    const bool saved = file.open(QIODevice::WriteOnly)
                       && file.write(QJsonDocument(layoutFile(m_choice->layout, *m_choice->drive)).toJson()) >= 0
                       && file.commit();
    if (!saved) {
        m_error->setText(tr("The layout could not be saved to %1, so nothing was written.").arg(m_choice->layoutPath));
        m_error->show();
    }
    return saved;
}

} // namespace gw
