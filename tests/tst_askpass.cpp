// The password box, on Qt's offscreen display: it shows sudo's prompt,
// masks what is typed, and hands back the password only on OK.
#include "gui/askpassdialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QtTest>

class TstAskpass : public QObject
{
    Q_OBJECT
private slots:
    void showsSudosPromptAndMasksTheField()
    {
        gw::AskpassDialog d(QStringLiteral("[sudo] password for root: "));
        bool shown = false;
        for (auto *label : d.findChildren<QLabel *>())
            shown = shown || label->text() == QLatin1String("[sudo] password for root:");
        QVERIFY(shown);
        QCOMPARE(d.passwordField()->echoMode(), QLineEdit::Password);
    }

    void okReturnsWhatWasTyped()
    {
        gw::AskpassDialog d(QStringLiteral("[sudo] password for root: "));
        d.show();
        QTest::keyClicks(d.passwordField(), QStringLiteral("s3cret"));
        d.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(d.result(), int(QDialog::Accepted));
        QCOMPARE(d.password(), QStringLiteral("s3cret"));
    }

    void cancelRejects()
    {
        gw::AskpassDialog d(QStringLiteral("prompt"));
        d.show();
        d.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        QCOMPARE(d.result(), int(QDialog::Rejected));
    }
};

QTEST_MAIN(TstAskpass)
#include "tst_askpass.moc"
