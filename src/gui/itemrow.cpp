#include "itemrow.h"

#include "core/checkrunner.h"

#include <QCheckBox>
#include <QEvent>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace gw {

ItemRow::ItemRow(const Item &item, const CheckResult &result, QWidget *parent)
    : QWidget(parent), m_id(item.id()), m_toggle(new QCheckBox(item.title(), this)),
      m_reason(new QLabel(this))
{
    QString state = stateText(result.state);
    if (!result.detail.isEmpty())
        state += QStringLiteral(" — ") + result.detail;
    auto *stateLabel = new QLabel(state, this);
    auto *sentence = new QLabel(item.applySentence(), this);
    for (QLabel *label : {stateLabel, sentence, m_reason})
        label->setWordWrap(true);
    m_reason->hide();

    // Nothing to apply where the item does not apply to this system.
    m_toggle->setEnabled(result.state != CheckState::NotNeeded);
    m_toggle->setAccessibleDescription(sentence->text());
    connect(m_toggle, &QCheckBox::toggled, this, [this](bool on) { emit toggledByUser(m_id, on); });

    applyFontScale();
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_toggle);
    layout->addWidget(stateLabel);
    layout->addWidget(sentence);
    layout->addWidget(m_reason);
}

void ItemRow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        applyFontScale();
}

void ItemRow::applyFontScale()
{
    QFont bold = font();
    bold.setBold(true);
    m_toggle->setFont(bold);
    const int side = fontMetrics().height();
    m_toggle->setStyleSheet(QStringLiteral("QCheckBox::indicator { width: %1px; height: %1px; }").arg(side));
}

bool ItemRow::isChecked() const { return m_toggle->isChecked(); }

void ItemRow::setChecked(bool on)
{
    const QSignalBlocker block(m_toggle);
    m_toggle->setChecked(on);
}

void ItemRow::setReason(const QString &reason)
{
    m_reason->setText(reason);
    m_reason->setVisible(!reason.isEmpty());
}

QString ItemRow::reason() const { return m_reason->text(); }

} // namespace gw
