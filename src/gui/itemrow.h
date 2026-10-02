// One item's row: a toggle named for the item, its state, the sentence
// saying what applying it would do (S3), and why it was switched on or off
// for another item (docs/design.md, Dependencies are kept by both sides).
#pragma once

#include "core/item.h"

#include <QWidget>

class QCheckBox;
class QLabel;

namespace gw {

class ItemRow : public QWidget
{
    Q_OBJECT
public:
    ItemRow(const Item &item, const CheckResult &result, QWidget *parent = nullptr);

    QString id() const { return m_id; }
    bool isChecked() const;
    // Sets the toggle without reporting it as the user's choice.
    void setChecked(bool on);
    // Shown under the row; empty hides it.
    void setReason(const QString &reason);
    QString reason() const;
    QCheckBox *toggle() const { return m_toggle; }

signals:
    void toggledByUser(const QString &id, bool on);

protected:
    void changeEvent(QEvent *event) override;

private:
    // The name in bold, and a tick box as tall as the text, so both stay
    // easy to see at large font sizes (design, Text).
    void applyFontScale();

    QString m_id;
    QCheckBox *m_toggle;
    QLabel *m_reason;
};

} // namespace gw
