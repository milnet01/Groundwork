#include "switch.h"

#include <QEvent>
#include <QPainter>
#include <QStyle>

namespace gw {

Switch::Switch(const QString &text, QWidget *parent) : QCheckBox(text, parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

int Switch::margin() const { return qMax(3, fontMetrics().height() / 6); }

QRect Switch::trackRect() const
{
    const int side = fontMetrics().height();
    return {margin(), (height() - side) / 2, 2 * side, side};
}

QSize Switch::sizeHint() const
{
    const QFontMetrics metrics = fontMetrics();
    const int side = metrics.height();
    return {2 * margin() + 2 * side + side / 2 + metrics.horizontalAdvance(text()), side + 2 * margin()};
}

bool Switch::hitButton(const QPoint &pos) const { return rect().contains(pos); }

void Switch::changeEvent(QEvent *event)
{
    QCheckBox::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        updateGeometry();
}

void Switch::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette &pal = palette();
    const bool on = isChecked();
    // The theme sets every colour group alike, so dim a switch that cannot
    // be used, as its text is.
    if (!isEnabled())
        painter.setOpacity(0.45);

    const QRect track = QStyle::visualRect(layoutDirection(), rect(), trackRect());
    const qreal radius = track.height() / 2.0;
    const qreal line = qMax(1.5, track.height() / 12.0);
    const QRectF inner = QRectF(track).adjusted(line / 2, line / 2, -line / 2, -line / 2);
    if (on) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(pal.color(QPalette::Highlight));
        painter.drawRoundedRect(track, radius, radius);
    } else {
        painter.setPen(QPen(pal.color(QPalette::WindowText), line));
        painter.setBrush(pal.color(QPalette::Base));
        painter.drawRoundedRect(inner, radius - line / 2, radius - line / 2);
    }

    // The knob sits at the end of the track when on, at its start when off.
    const qreal inset = track.height() / 6.0;
    const qreal knob = track.height() - 2 * inset;
    const bool atRight = on != (layoutDirection() == Qt::RightToLeft);
    const qreal knobX = atRight ? track.right() + 1 - inset - knob : track.left() + inset;
    painter.setPen(Qt::NoPen);
    painter.setBrush(pal.color(on ? QPalette::HighlightedText : QPalette::WindowText));
    painter.drawEllipse(QRectF(knobX, track.top() + inset, knob, knob));

    // The focus ring in the text's colour, which every theme keeps readable.
    if (hasFocus()) {
        const qreal gap = margin() / 2.0;
        painter.setPen(QPen(pal.color(QPalette::WindowText), qMax(1.0, gap)));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(track).adjusted(-gap, -gap, gap, gap), radius + gap, radius + gap);
    }

    painter.setOpacity(1.0);
    QRect label = rect();
    label.setLeft(trackRect().right() + 1 + track.height() / 2);
    label = QStyle::visualRect(layoutDirection(), rect(), label);
    const Qt::Alignment align = QStyle::visualAlignment(layoutDirection(), Qt::AlignLeft | Qt::AlignVCenter);
    style()->drawItemText(&painter, label, align | Qt::TextShowMnemonic, pal, isEnabled(), text(),
                          QPalette::WindowText);
}

} // namespace gw
