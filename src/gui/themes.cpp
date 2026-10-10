#include "themes.h"

#include <QApplication>
#include <QCoreApplication>
#include <QStyleHints>

#include <algorithm>
#include <cmath>
#include <optional>

namespace gw {
namespace {

// Light, Dark and the named themes come from Ants Terminal's themes.cpp;
// Midnight, Emerald and the high-contrast pair from LocalWebServerManager's
// theme.py, whose values were tuned to contrast floors. Muted text is the
// placeholder in an empty field, so it must read too.
Theme make(const char *id, const QString &name, const char *window, const char *base, const char *altBase,
           const char *text, const char *muted, const char *accent, bool highContrast = false)
{
    return {QString::fromLatin1(id), name, QColor(window), QColor(base), QColor(altBase),
            QColor(text), QColor(muted), QColor(accent), highContrast};
}

QColor mix(const QColor &a, const QColor &b, double towardB)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * towardB),
                            float(a.greenF() + (b.greenF() - a.greenF()) * towardB),
                            float(a.blueF() + (b.blueF() - a.blueF()) * towardB));
}

double channel(double c)
{
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(const QColor &c)
{
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}

bool isDark(const QPalette &palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

QString &current()
{
    static QString id = QString::fromLatin1(kFollowDesktop);
    return id;
}

std::optional<Theme> find(const QString &id)
{
    for (const Theme &theme : themes())
        if (theme.id == id)
            return theme;
    return std::nullopt;
}

} // namespace

QList<Theme> themes()
{
    // Proper names stay as they are in every language.
    return {
        {QString::fromLatin1(kFollowDesktop), QCoreApplication::translate("gw::Themes", "Follow the desktop"), {}, {}, {}, {}, {}, {}, false},
        make("light", QCoreApplication::translate("gw::Themes", "Light"), "#f5f5f5", "#ffffff", "#ededed", "#1e1e2e", "#4a4a5a", "#1a73e8"),
        make("dark", QCoreApplication::translate("gw::Themes", "Dark"), "#1e1e2e", "#181825", "#26263a", "#cdd6f4", "#a6adc8", "#89b4fa"),
        make("high-contrast-light", QCoreApplication::translate("gw::Themes", "High contrast, light"), "#ffffff", "#ffffff", "#f2f2f2", "#000000",
             "#3d3d3d", "#0000cc", true),
        make("high-contrast-dark", QCoreApplication::translate("gw::Themes", "High contrast, dark"), "#000000", "#000000", "#141414", "#ffffff",
             "#d0d0d0", "#ffff00", true),
        make("midnight", QCoreApplication::translate("gw::Themes", "Midnight"), "#131a2b", "#0d1320", "#1b2336", "#e6e9f0", "#8b93a7", "#d4af37"),
        make("emerald", QCoreApplication::translate("gw::Themes", "Emerald"), "#102019", "#0a1712", "#172b22", "#dcece4", "#7d9a8c", "#1fae6a"),
        make("nord", "Nord", "#2e3440", "#272c36", "#3b4252", "#eceff4", "#d8dee9", "#88c0d0"),
        make("dracula", "Dracula", "#282a36", "#21222c", "#343746", "#f8f8f2", "#bfbfbf", "#bd93f9"),
        make("solarized-dark", "Solarized Dark", "#002b36", "#001e27", "#073642", "#93a1a1", "#839496",
             "#268bd2"),
        make("gruvbox", "Gruvbox", "#282828", "#1d2021", "#32302f", "#ebdbb2", "#d5c4a1", "#fabd2f"),
        make("monokai", "Monokai", "#272822", "#1e1f1c", "#3e3d32", "#f8f8f2", "#c0c0b0", "#66d9ef"),
        make("tokyo-night", "Tokyo Night", "#1a1b26", "#16161e", "#24283b", "#c0caf5", "#a9b1d6", "#7aa2f7"),
        // Its blue darkened from #1e66f5, which held selected text at 4.0:1.
        make("catppuccin-latte", "Catppuccin Latte", "#eff1f5", "#e6e9ef", "#dce0e8", "#4c4f69", "#5c5f77",
             "#1e5ad6"),
    };
}

QPalette paletteFor(const Theme &theme)
{
    QPalette palette(theme.window, theme.window); // derives the bevel shades
    palette.setColor(QPalette::Window, theme.window);
    palette.setColor(QPalette::WindowText, theme.text);
    palette.setColor(QPalette::Base, theme.base);
    palette.setColor(QPalette::AlternateBase, theme.altBase);
    palette.setColor(QPalette::Text, theme.text);
    palette.setColor(QPalette::Button, theme.window);
    palette.setColor(QPalette::ButtonText, theme.text);
    palette.setColor(QPalette::BrightText, theme.text);
    palette.setColor(QPalette::PlaceholderText, theme.muted);
    palette.setColor(QPalette::ToolTipBase, theme.base);
    palette.setColor(QPalette::ToolTipText, theme.text);
    palette.setColor(QPalette::Link, theme.accent);
    palette.setColor(QPalette::LinkVisited, theme.accent);
    palette.setColor(QPalette::Highlight, theme.accent);
    // Whichever reads better on the accent.
    palette.setColor(QPalette::HighlightedText, contrast(theme.base, theme.accent) >= contrast(theme.text, theme.accent)
                                                    ? theme.base
                                                    : theme.text);
    // The calls above set every group alike, so a switched-off control
    // would look live. Dim its text toward the window.
    const QColor dim = mix(theme.text, theme.window, 0.45);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, dim);
    return palette;
}

double contrast(const QColor &a, const QColor &b)
{
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

bool desktopIsDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    switch (QGuiApplication::styleHints()->colorScheme()) {
    case Qt::ColorScheme::Dark: return true;
    case Qt::ColorScheme::Light: return false;
    default: break; // unknown: ask the palette
    }
#endif
    // Before any theme, or after following the desktop, the application's
    // palette is the platform's.
    return current() == QLatin1String(kFollowDesktop) && isDark(QApplication::palette());
}

void applyTheme(const QString &id)
{
    const std::optional<Theme> theme = find(id);
    if (theme && theme->id != QLatin1String(kFollowDesktop)) {
        current() = theme->id;
        QApplication::setPalette(paletteFor(*theme));
        return;
    }
    current() = QString::fromLatin1(kFollowDesktop);
    // A palette with no colours set is the platform's, and Qt keeps it in
    // step with the desktop.
    QApplication::setPalette(QPalette());
    if (desktopIsDark() && !isDark(QApplication::palette()))
        QApplication::setPalette(paletteFor(*find(QStringLiteral("dark"))));

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // Follow the desktop when it changes while the window is open.
    static const bool watching = [] {
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, [] {
            if (current() == QLatin1String(kFollowDesktop))
                applyTheme(QString::fromLatin1(kFollowDesktop));
        });
        return true;
    }();
    (void)watching;
#endif
}

QString currentTheme()
{
    return current();
}

} // namespace gw
