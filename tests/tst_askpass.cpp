// The password box, on Qt's offscreen display: it says in plain words whose
// password it wants, masks what is typed, and hands back the password only
// on OK.
#include "core/translations.h"
#include "gui/askpassdialog.h"
#include "gui/passwordchannel.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QtTest>

#include <thread>

class TstAskpass : public QObject
{
    Q_OBJECT
private slots:
    static QString allText(const gw::AskpassDialog &d)
    {
        QStringList texts;
        for (auto *label : d.findChildren<QLabel *>())
            texts << label->text();
        return texts.join(QLatin1Char('\n'));
    }

    // sudo passes only whose password it wants, not its raw "[sudo]
    // password for root:" line (GRND-0046). Tumbleweed asks for root's.
    void namesRootsPasswordAsTheAdministrators()
    {
        gw::AskpassDialog d(QStringLiteral("root"));
        const QString text = allText(d);
        QVERIFY2(text.contains(QLatin1String("administrator password")), qPrintable(text));
        QVERIFY2(!text.contains(QLatin1String("[sudo]")), qPrintable(text));
        QCOMPARE(d.passwordField()->echoMode(), QLineEdit::Password);
    }

    // Leap 16 asks for the user's own.
    void namesAUsersPasswordAsTheirOwn()
    {
        gw::AskpassDialog d(QStringLiteral("tester"));
        const QString text = allText(d);
        QVERIFY2(text.contains(QLatin1String("your password")), qPrintable(text));
        QVERIFY2(text.contains(QLatin1String("tester")), qPrintable(text));
    }

    // A prompt that is not a bare name, such as PAM's own, is shown as it is.
    void showsAnyOtherPromptAsItIs()
    {
        gw::AskpassDialog d(QStringLiteral("Password for root: "));
        QVERIFY(allText(d).contains(QLatin1String("Password for root:")));
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

    // Qt has no words of its own for Afrikaans, so the box labels its own
    // buttons (GRND-0033). "OK" is also Afrikaans, so Cancel is the probe.
    void itsButtonsSpeakTheChosenLanguage()
    {
        QVERIFY(gw::loadLanguage(QStringLiteral("af")));
        gw::AskpassDialog d(QStringLiteral("prompt"));
        const QString cancel = d.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->text();
        gw::loadLanguage(QStringLiteral("en"));
        QVERIFY2(cancel != QLatin1String("Cancel") && cancel != QLatin1String("&Cancel"), qPrintable(cancel));
    }

    // Askpass mode asks the window, whose own box is its child, so a click
    // on the window cannot hide it (GRND-0102).
    void theWindowShowsTheBoxAsItsOwn()
    {
        QWidget window;
        window.show();
        gw::PasswordChannel channel(&window);
        QVERIFY(channel.listen());
        // The socket's folder is the user's alone.
        QCOMPARE(QFileInfo(QFileInfo(channel.path()).path()).permissions() & 0x0077, QFileDevice::Permissions());

        QByteArray reply;
        gw::Asked asked = gw::Asked::NoChannel;
        std::thread askpass([&] { asked = gw::askWindow(channel.path(), QStringLiteral("root"), &reply); });
        gw::AskpassDialog *box = nullptr;
        QTRY_VERIFY((box = window.findChild<gw::AskpassDialog *>()));
        QCOMPARE(box->parentWidget(), &window);
        QVERIFY(box->isModal());
        QVERIFY2(allText(*box).contains(QLatin1String("administrator password")), qPrintable(allText(*box)));
        QTest::keyClicks(box->passwordField(), QStringLiteral("s3cret"));
        box->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        askpass.join();
        QCOMPARE(asked, gw::Asked::Answered);
        QCOMPARE(reply, QByteArray("s3cret\n"));
    }

    void cancelInTheWindowReadsAsCancel()
    {
        QWidget window;
        gw::PasswordChannel channel(&window);
        QVERIFY(channel.listen());
        QByteArray reply;
        gw::Asked asked = gw::Asked::NoChannel;
        std::thread askpass([&] { asked = gw::askWindow(channel.path(), QStringLiteral("root"), &reply); });
        gw::AskpassDialog *box = nullptr;
        QTRY_VERIFY((box = window.findChild<gw::AskpassDialog *>()));
        box->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        askpass.join();
        QCOMPARE(asked, gw::Asked::Cancelled);
    }

    // With no window listening, askpass mode shows its own box, as before.
    void noWindowLeavesTheBoxToAskpass()
    {
        QWidget window;
        gw::PasswordChannel channel(&window);
        QVERIFY(channel.listen());
        const QString path = channel.path();
        channel.close();
        QVERIFY(!QFileInfo::exists(QFileInfo(path).path()));
        QByteArray reply;
        QCOMPARE(gw::askWindow(path, QStringLiteral("root"), &reply), gw::Asked::NoChannel);
    }
};

QTEST_MAIN(TstAskpass)
#include "tst_askpass.moc"
