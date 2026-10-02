#include "askpassdialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#include <cstdio>

namespace gw {

AskpassDialog::AskpassDialog(const QString &sudoPrompt, QWidget *parent)
    : QDialog(parent), m_field(new QLineEdit(this))
{
    setWindowTitle(tr("Groundwork"));
    auto *why = new QLabel(tr("Groundwork needs administrator rights to make the changes you chose."), this);
    why->setWordWrap(true);
    // sudo's own prompt says whose password it wants.
    auto *prompt = new QLabel(sudoPrompt.trimmed(), this);
    prompt->setWordWrap(true);
    prompt->setBuddy(m_field);
    m_field->setEchoMode(QLineEdit::Password);
    m_field->setAccessibleName(tr("Password"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(why);
    layout->addWidget(prompt);
    layout->addWidget(m_field);
    layout->addWidget(buttons);
    m_field->setFocus();
}

QString AskpassDialog::password() const { return m_field->text(); }

int runAskpass(const QString &sudoPrompt)
{
    AskpassDialog dialog(sudoPrompt);
    if (dialog.exec() != QDialog::Accepted)
        return 1;
    const QByteArray bytes = dialog.password().toUtf8() + '\n';
    std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    std::fflush(stdout);
    return 0;
}

} // namespace gw
