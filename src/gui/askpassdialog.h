// The app's own password box (GRND-0007): the program sudo -A calls, so the
// app needs no desktop's password helper. It shows sudo's own prompt, which
// names whose password it wants (docs/design.md, Root).
#pragma once

#include <QDialog>

class QLineEdit;

namespace gw {

class AskpassDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AskpassDialog(const QString &sudoPrompt, QWidget *parent = nullptr);
    QString password() const;
    QLineEdit *passwordField() const { return m_field; }

private:
    QLineEdit *m_field;
};

// Askpass mode: shows the box; on OK prints the password and a newline to
// standard output for sudo and returns 0, on Cancel returns 1.
int runAskpass(const QString &sudoPrompt);

} // namespace gw
