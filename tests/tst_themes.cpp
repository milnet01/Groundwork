// Colour themes (GRND-0050), on Qt's offscreen display: every theme reads,
// a theme paints the whole application, and following the desktop gives
// back the platform's palette.
#include "gui/themes.h"

#include <QApplication>
#include <QSet>
#include <QtTest>

class TestThemes : public QObject
{
    Q_OBJECT

private slots:
    void cleanup() { gw::applyTheme(QString::fromLatin1(gw::kFollowDesktop)); }

    // The desktop's choice comes first, and the user's asks are offered:
    // dark, and high contrast both ways.
    void theDesktopComesFirstAndTheAsksAreOffered()
    {
        const QList<gw::Theme> all = gw::themes();
        QVERIFY(!all.isEmpty());
        QCOMPARE(all.first().id, QString::fromLatin1(gw::kFollowDesktop));
        QSet<QString> ids;
        for (const gw::Theme &theme : all) {
            QVERIFY2(!ids.contains(theme.id), qPrintable(theme.id));
            ids.insert(theme.id);
            QVERIFY2(!theme.name.isEmpty(), qPrintable(theme.id));
        }
        for (const char *id : {"light", "dark", "high-contrast-light", "high-contrast-dark"})
            QVERIFY2(ids.contains(QString::fromLatin1(id)), id);
    }

    // WCAG 2.1 AA: text at 4.5:1 on every background it sits on, the
    // placeholder too; AAA, 7:1, for high contrast.
    void everyThemeReads()
    {
        for (const gw::Theme &theme : gw::themes()) {
            if (theme.id == QLatin1String(gw::kFollowDesktop))
                continue;
            const QPalette p = gw::paletteFor(theme);
            const double floor = theme.highContrast ? 7.0 : 4.5;
            const QList<std::pair<QPalette::ColorRole, QPalette::ColorRole>> pairs{
                {QPalette::WindowText, QPalette::Window},
                {QPalette::Text, QPalette::Base},
                {QPalette::Text, QPalette::AlternateBase},
                {QPalette::ButtonText, QPalette::Button},
                {QPalette::PlaceholderText, QPalette::Base},
                {QPalette::HighlightedText, QPalette::Highlight},
            };
            for (const auto &[fore, back] : pairs) {
                const double ratio = gw::contrast(p.color(fore), p.color(back));
                QVERIFY2(ratio >= floor, qPrintable(QStringLiteral("%1: %2 on %3 is %4")
                                                        .arg(theme.id)
                                                        .arg(int(fore))
                                                        .arg(int(back))
                                                        .arg(ratio, 0, 'f', 2)));
            }
            // A switched-off control looks switched off.
            QVERIFY2(p.color(QPalette::Disabled, QPalette::Text) != p.color(QPalette::Active, QPalette::Text),
                     qPrintable(theme.id));
        }
    }

    void contrastMatchesWcag()
    {
        QCOMPARE(gw::contrast(Qt::black, Qt::white), 21.0);
        QCOMPARE(gw::contrast(Qt::white, Qt::white), 1.0);
    }

    // A chosen theme paints the application; following the desktop gives
    // back exactly the platform's palette, here light.
    void aThemePaintsTheAppAndTheDesktopGivesItBack()
    {
        const QPalette platform = QApplication::palette();
        QVERIFY(platform.color(QPalette::Window).lightness() >= 128);

        gw::applyTheme(QStringLiteral("dark"));
        QCOMPARE(gw::currentTheme(), QStringLiteral("dark"));
        QVERIFY(QApplication::palette().color(QPalette::Window).lightness() < 128);

        gw::applyTheme(QString::fromLatin1(gw::kFollowDesktop));
        QCOMPARE(gw::currentTheme(), QString::fromLatin1(gw::kFollowDesktop));
        QCOMPARE(QApplication::palette(), platform);
    }

    void anUnknownThemeFollowsTheDesktop()
    {
        gw::applyTheme(QStringLiteral("dark"));
        gw::applyTheme(QStringLiteral("no-such-theme"));
        QCOMPARE(gw::currentTheme(), QString::fromLatin1(gw::kFollowDesktop));
    }

    // GRND-0072: a theme chosen in one run is the theme the next run
    // starts in, and the password box is told it too.
    void aChosenThemeIsKeptForTheNextRun()
    {
        QTemporaryDir config;
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        gw::chooseTheme(QStringLiteral("nord"));
        QVERIFY(QFile::exists(config.filePath(QStringLiteral("groundwork/settings.conf"))));

        // The next run starts from nothing applied and nothing handed on.
        gw::applyTheme(QString::fromLatin1(gw::kFollowDesktop));
        qunsetenv("GROUNDWORK_THEME");
        gw::restoreTheme();
        QCOMPARE(gw::currentTheme(), QStringLiteral("nord"));
        QCOMPARE(qgetenv("GROUNDWORK_THEME"), QByteArray("nord"));

        // Choosing the desktop again is kept too.
        gw::chooseTheme(QString::fromLatin1(gw::kFollowDesktop));
        gw::applyTheme(QStringLiteral("dark"));
        gw::restoreTheme();
        QCOMPARE(gw::currentTheme(), QString::fromLatin1(gw::kFollowDesktop));
        qunsetenv("XDG_CONFIG_HOME");
        qunsetenv("GROUNDWORK_THEME");
    }

    // With nothing saved, the first run follows the desktop.
    void nothingSavedFollowsTheDesktop()
    {
        QTemporaryDir config;
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        gw::applyTheme(QStringLiteral("dark"));
        gw::restoreTheme();
        QCOMPARE(gw::currentTheme(), QString::fromLatin1(gw::kFollowDesktop));
        QCOMPARE(qgetenv("GROUNDWORK_THEME"), QByteArray(gw::kFollowDesktop));
        qunsetenv("XDG_CONFIG_HOME");
        qunsetenv("GROUNDWORK_THEME");
    }
};

QTEST_MAIN(TestThemes)
#include "tst_themes.moc"
