// One item's row: an on/off switch named for the item, its state, the sentence
// saying what applying it would do (S3), and why it was switched on or off
// for another item (docs/design.md, Dependencies are kept by both sides).
#pragma once

#include "core/item.h"

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;

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
    // For an item that takes a value; null otherwise.
    QLineEdit *valueField() const { return m_value; }
    QString value() const;

signals:
    void toggledByUser(const QString &id, bool on);

protected:
    void changeEvent(QEvent *event) override;

private:
    // The name in bold, so it stays easy to see at large font sizes
    // (design, Text). The switch sizes itself from the font.
    void applyFontScale();

    QString m_id;
    QCheckBox *m_toggle;
    QLabel *m_reason;
    QLineEdit *m_value = nullptr;
};

} // namespace gw
