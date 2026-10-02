#include "runpage.h"

#include "core/markers.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

namespace gw {

RunPage::RunPage(QWidget *parent)
    : QWizardPage(parent), m_rows(new QVBoxLayout), m_hintLabel(new QLabel(this)),
      m_summary(new QLabel(this)), m_details(new QPlainTextEdit(this)),
      m_oneUp(new QPushButton(tr("Open OneUp to keep this computer up to date"), this))
{
    setTitle(tr("Setting up"));
    setSubTitle(tr("You can close this window; Groundwork then stops after the current step."));
    m_hintLabel->setWordWrap(true);
    m_summary->setWordWrap(true);
    m_details->setReadOnly(true);
    m_details->hide();
    // OneUp keeps a machine up to date after setup (design, The levels).
    // Its program is `oneup` (OneUp's data/za.co.antsprojectshub.OneUp.desktop).
    m_oneUp->hide();
    connect(m_oneUp, &QPushButton::clicked, this, [] {
        QProcess::startDetached(QStandardPaths::findExecutable(QStringLiteral("oneup")), {});
    });
    auto *toggle = new QToolButton(this);
    toggle->setText(tr("Show details"));
    toggle->setCheckable(true);
    connect(toggle, &QToolButton::toggled, m_details, &QWidget::setVisible);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(m_rows);
    layout->addWidget(m_hintLabel);
    layout->addWidget(m_summary);
    layout->addWidget(m_oneUp);
    layout->addWidget(toggle);
    layout->addWidget(m_details);

    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &RunPage::readOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this,
            [this] { m_details->appendPlainText(QString::fromUtf8(m_process.readAllStandardError())); });
    connect(&m_process, &QProcess::finished, this, [this] {
        readOutput();
        if (!m_pending.isEmpty())
            handleLine(m_pending);
        m_pending.clear();
        if (m_summary->text().isEmpty())
            m_summary->setText(tr("Groundwork stopped unexpectedly. The details below say why."));
        m_oneUp->setVisible(!QStandardPaths::findExecutable(QStringLiteral("oneup")).isEmpty());
        emit completeChanged();
        emit runFinished();
    });
}

void RunPage::start(const QString &program, const QStringList &arguments,
                    const QList<QPair<QString, QString>> &titles)
{
    for (const auto &[id, title] : titles) {
        auto *label = new QLabel(tr("%1: waiting").arg(title), this);
        label->setWordWrap(true);
        label->setProperty("title", title);
        m_rows->addWidget(label);
        m_status.insert(id, label);
    }
    m_started = true;
    m_process.start(program, arguments);
    emit completeChanged();
}

bool RunPage::isComplete() const { return m_started && !isRunning(); }

QString RunPage::statusOf(const QString &id) const
{
    const QLabel *label = m_status.value(id);
    return label ? label->text() : QString();
}

QString RunPage::summary() const { return m_summary->text(); }

void RunPage::readOutput()
{
    m_pending += QString::fromUtf8(m_process.readAllStandardOutput());
    qsizetype nl;
    while ((nl = m_pending.indexOf(QLatin1Char('\n'))) >= 0) {
        handleLine(m_pending.left(nl));
        m_pending.remove(0, nl + 1);
    }
}

void RunPage::handleLine(const QString &line)
{
    const auto marker = parseMarker(line);
    if (!marker) {
        m_details->appendPlainText(line);
        return;
    }
    const QStringList &f = marker->fields;
    auto setStatus = [this](const QString &id, const QString &text) {
        if (QLabel *label = m_status.value(id))
            label->setText(tr("%1: %2").arg(label->property("title").toString(), text));
    };
    if (marker->name == QLatin1String("STEP_BEGIN")) {
        setStatus(f.value(0), tr("working…"));
    } else if (marker->name == QLatin1String("ACTION")) {
        setStatus(f.value(0), f.value(1));
    } else if (marker->name == QLatin1String("STEP_END")) {
        const QString result = f.value(1);
        QString text = result == QLatin1String("ok")     ? tr("done")
                       : result == QLatin1String("skip") ? tr("skipped")
                                                          : tr("failed");
        if (!f.value(2).isEmpty())
            text += QStringLiteral(" — ") + f.value(2);
        setStatus(f.value(0), text);
    } else if (marker->name == QLatin1String("HINT")) {
        m_hints << f.value(1);
        m_hintLabel->setText(m_hints.join(QLatin1Char('\n')));
    } else if (marker->name == QLatin1String("AUTH") && f.value(0) == QLatin1String("failed")) {
        m_summary->setText(tr("The password was not accepted, so nothing was changed."));
    } else if (marker->name == QLatin1String("UNSUPPORTED")) {
        m_summary->setText(f.value(0));
    } else if (marker->name == QLatin1String("DONE")) {
        const int failed = f.value(1).toInt();
        if (f.value(2) == QLatin1String("1"))
            m_summary->setText(tr("Stopped. Nothing after the last finished step was changed."));
        else if (failed > 0)
            m_summary->setText(tr("Finished, but %n item(s) could not be completed.", nullptr, failed));
        else
            m_summary->setText(tr("All done."));
    }
}

} // namespace gw
