// Language choice and direction, and the pseudo-translation proof that the
// text a user sees is translatable at its source (GRND-0032; S3).
#include "core/translations.h"
#include "items/catalogue.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTranslator>
#include <QXmlStreamReader>
#include <QtTest>

namespace {

// Each message in a .ts file as "context|source" (GRND-0038).
QSet<QString> tsMessages(const QString &path)
{
    QSet<QString> keys;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return keys;
    QXmlStreamReader xml(&file);
    QString context;
    while (xml.readNextStartElement() || !xml.atEnd()) {
        if (!xml.isStartElement())
            continue;
        if (xml.name() == QLatin1String("name"))
            context = xml.readElementText();
        else if (xml.name() == QLatin1String("source"))
            keys.insert(context + QLatin1Char('|') + xml.readElementText());
    }
    return keys;
}

QSet<QString> contexts(const QSet<QString> &messages)
{
    QSet<QString> names;
    for (const QString &key : messages)
        names.insert(key.section(QLatin1Char('|'), 0, 0));
    return names;
}

// What lupdate extracts from the source now, as the update_translations
// target runs it.
QSet<QString> extractNow()
{
    QTemporaryDir dir;
    const QString ts = dir.filePath(QStringLiteral("groundwork_xx.ts"));
    QProcess p;
    p.start(QStringLiteral(LUPDATE_BIN), {QStringLiteral("-silent"), QStringLiteral("-locations"),
                                          QStringLiteral("none"), QStringLiteral(SOURCE_DIR "/src"),
                                          QStringLiteral("-ts"), ts});
    if (!p.waitForFinished(60000) || p.exitCode() != 0)
        return {};
    return tsMessages(ts);
}

} // namespace

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

    void translationFilesMatchTheSource() // refresh with the update_translations target
    {
        const QSet<QString> now = extractNow();
        QVERIFY(!now.isEmpty());
        const QStringList files = QDir(QStringLiteral(SOURCE_DIR "/translations"))
                                      .entryList({QStringLiteral("groundwork_*.ts")}, QDir::Files);
        QVERIFY(!files.isEmpty());
        for (const QString &file : files)
            QVERIFY2(tsMessages(QStringLiteral(SOURCE_DIR "/translations/") + file) == now, qPrintable(file));
    }

    // lupdate files a string under a context only from what it can see at
    // the call. A string it files under another name than the code looks up
    // is never translated.
    void extractionUsesTheContextsTheCodeLooksUp()
    {
        QSet<QString> lookedUp, qobjects;
        static const QRegularExpression named(
            QStringLiteral("(?:translate|QT_TRANSLATE_NOOP)\\(\\s*\"([^\"]+)\""));
        static const QRegularExpression klass(QStringLiteral("class (\\w+)\\s*:"));
        QDirIterator it(QStringLiteral(SOURCE_DIR "/src"), {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QFile file(it.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(file.readAll());
            for (const auto &m : named.globalMatch(text))
                lookedUp.insert(m.captured(1));
            if (text.contains(QStringLiteral("Q_OBJECT")))
                for (const auto &m : klass.globalMatch(text))
                    qobjects.insert(QStringLiteral("gw::") + m.captured(1));
        }
        const QSet<QString> extracted = contexts(extractNow());
        QVERIFY(!extracted.isEmpty());
        for (const QString &name : extracted)
            QVERIFY2(lookedUp.contains(name) || qobjects.contains(name), qPrintable(name));
        for (const QString &name : lookedUp)
            QVERIFY2(extracted.contains(name), qPrintable(name));
    }

    void onlyTranslatedLanguagesAreOffered() // an empty .qm would show English
    {
        const QStringList files = QDir(QStringLiteral(":/i18n"))
                                      .entryList({QStringLiteral("groundwork_*.qm")}, QDir::Files);
        QVERIFY(!files.isEmpty());
        const QStringList offered = gw::availableLanguages();
        for (const QString &file : files) {
            QTranslator t;
            QVERIFY2(t.load(file, QStringLiteral(":/i18n")), qPrintable(file));
            const QString code = file.mid(11, file.size() - 11 - 3);
            QVERIFY2(offered.contains(code) == !t.isEmpty(), qPrintable(file));
        }
    }
};

QTEST_GUILESS_MAIN(TstTranslations)
#include "tst_translations.moc"
