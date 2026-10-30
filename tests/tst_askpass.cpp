// The password box, on Qt's offscreen display: it says in plain words whose
// password it wants, masks what is typed, and hands back the password only
// on OK.
#include "core/translations.h"
#include "gui/askpassdialog.h"
#include "gui/passwordchannel.h"
#include "gui/runpage.h"

#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
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

    // Askpass mode asks the window, which answers it (GRND-0102).
    void theWindowAnswersAskpassMode()
    {
        gw::PasswordChannel channel;
        QVERIFY(channel.listen());
        // The socket's folder is the user's alone.
        QCOMPARE(QFileInfo(QFileInfo(channel.path()).path()).permissions() & 0x0077, QFileDevice::Permissions());
        QString prompt;
        connect(&channel, &gw::PasswordChannel::asked, &channel, [&](const QString &p) {
            prompt = p;
            channel.reply(QStringLiteral("s3cret"));
        });

        QByteArray reply;
        gw::Asked asked = gw::Asked::NoChannel;
        std::thread askpass([&] { asked = gw::askWindow(channel.path(), QStringLiteral("root"), &reply); });
        QTRY_COMPARE(prompt, QStringLiteral("root"));
        askpass.join();
        QCOMPARE(asked, gw::Asked::Answered);
        QCOMPARE(reply, QByteArray("s3cret\n"));
    }

    void refusedInTheWindowReadsAsCancel()
    {
        gw::PasswordChannel channel;
        QVERIFY(channel.listen());
        connect(&channel, &gw::PasswordChannel::asked, &channel, [&] { channel.refuse(); });
        QByteArray reply;
        gw::Asked asked = gw::Asked::NoChannel;
        std::thread askpass([&] { asked = gw::askWindow(channel.path(), QStringLiteral("root"), &reply); });
        QTRY_VERIFY(asked != gw::Asked::NoChannel);
        askpass.join();
        QCOMPARE(asked, gw::Asked::Cancelled);
    }

    // A second request before an answer replaces the first, which reads as
    // Cancel, so no askpass waits on a field that is gone.
    void aSecondRequestRefusesTheFirst()
    {
        gw::PasswordChannel channel;
        QVERIFY(channel.listen());
        int requests = 0;
        connect(&channel, &gw::PasswordChannel::asked, &channel, [&] { ++requests; });
        QByteArray first, second;
        gw::Asked firstAsked = gw::Asked::NoChannel;
        std::thread one([&] { firstAsked = gw::askWindow(channel.path(), QStringLiteral("root"), &first); });
        QTRY_COMPARE(requests, 1);
        std::thread two([&] { gw::askWindow(channel.path(), QStringLiteral("root"), &second); });
        QTRY_COMPARE(requests, 2);
        one.join();
        QCOMPARE(firstAsked, gw::Asked::Cancelled);
        channel.reply(QStringLiteral("s3cret"));
        two.join();
        QCOMPARE(second, QByteArray("s3cret\n"));
    }

    // The run page asks in a field of its own, not a box that pops up
    // (GRND-0108). The Worker stands in here as a shell that prints the
    // socket's name sudo would pass on.
    void theRunPageAsksInAFieldOfItsOwn()
    {
        gw::RunPage page;
        page.show();
        page.start(QStringLiteral("sh"),
                   {QStringLiteral("-c"), QStringLiteral("echo \"$GROUNDWORK_ASKPASS_SOCKET\"; sleep 10")}, {});
        auto *details = page.findChild<QPlainTextEdit *>();
        QTRY_VERIFY(!details->toPlainText().trimmed().isEmpty());
        const QString path = details->toPlainText().trimmed();

        QByteArray reply;
        gw::Asked asked = gw::Asked::NoChannel;
        std::thread askpass([&] { asked = gw::askWindow(path, QStringLiteral("root"), &reply); });
        gw::AskpassDialog *form = nullptr;
        QTRY_VERIFY((form = page.findChild<gw::AskpassDialog *>()) && form->isVisible());
        QVERIFY(!form->isWindow());
        QVERIFY2(allText(*form).contains(QLatin1String("administrator password")), qPrintable(allText(*form)));
        QTest::keyClicks(form->passwordField(), QStringLiteral("s3cret"));
        QTest::keyClick(form->passwordField(), Qt::Key_Return);
        askpass.join();
        QCOMPARE(asked, gw::Asked::Answered);
        QCOMPARE(reply, QByteArray("s3cret\n"));
        QTRY_VERIFY(!page.findChild<gw::AskpassDialog *>());
    }

    // With no window listening, askpass mode shows its own box, as before.
    void noWindowLeavesTheBoxToAskpass()
    {
        gw::PasswordChannel channel;
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
