// Language choice and direction, and the pseudo-translation proof that the
// text a user sees is translatable at its source (GRND-0032; S3).
#include "core/translations.h"
#include "items/catalogue.h"

#include <QCoreApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QtTest>

class TstTranslations : public QObject
{
    Q_OBJECT
private slots:
    void cleanup() { gw::loadLanguage(QStringLiteral("en")); }

    void englishIsAlwaysAvailable() { QVERIFY(gw::availableLanguages().contains(QStringLiteral("en"))); }

    void unknownLanguageKeepsTheSourceText()
    {
        QVERIFY(!gw::loadLanguage(QStringLiteral("xx")));
        QCOMPARE(QCoreApplication::translate("gw::FlathubItem", "Flatpak and Flathub"),
                 QStringLiteral("Flatpak and Flathub"));
    }

    void rightToLeftLanguagesMirror()
    {
        QCOMPARE(gw::directionFor(QStringLiteral("ar")), Qt::RightToLeft);
        QCOMPARE(gw::directionFor(QStringLiteral("he")), Qt::RightToLeft);
        QCOMPARE(gw::directionFor(QStringLiteral("af")), Qt::LeftToRight);
        QCOMPARE(gw::directionFor(QStringLiteral("zh_CN")), Qt::LeftToRight);
    }

    void everyItemsWordsAreTranslatable()
    {
        QVERIFY(gw::loadLanguage(gw::kPseudoLanguage));
        for (const gw::Item *item : gw::catalogue().items()) {
            QVERIFY2(item->title().startsWith(QStringLiteral("⟦")), qPrintable(item->id()));
            QVERIFY2(item->applySentence().startsWith(QStringLiteral("⟦")), qPrintable(item->id()));
        }
    }

    void everyLineOfCheckModeIsTranslatable()
    {
        QProcess p;
        QProcessEnvironment env;
        env.insert(QStringLiteral("GROUNDWORK_ROOT"), QStringLiteral(FIXTURES "/tumbleweed"));
        env.insert(QStringLiteral("PATH"), QStringLiteral(FAKES "/flathub-present"));
        p.setProcessEnvironment(env);
        p.start(QStringLiteral(GROUNDWORK_BIN), {QStringLiteral("--lang"), QStringLiteral("pseudo"),
                                                 QStringLiteral("--check")});
        QVERIFY(p.waitForFinished(30000));
        const QStringList lines = QString::fromUtf8(p.readAllStandardOutput())
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVERIFY(lines.size() >= 3);
        // With every translated piece removed, only spacing and the
        // separators check mode adds may remain.
        static const QRegularExpression translated(QStringLiteral("⟦[^⟧]*⟧"));
        static const QRegularExpression separatorsOnly(QStringLiteral("^[\\s:—]*$"));
        for (const QString &line : lines) {
            QString rest = line;
            rest.remove(translated);
            QVERIFY2(separatorsOnly.match(rest).hasMatch(), qPrintable(line));
        }
    }
};

QTEST_GUILESS_MAIN(TstTranslations)
#include "tst_translations.moc"
