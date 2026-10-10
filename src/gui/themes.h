// Colour themes (GRND-0050): the window follows the desktop's light or
// dark choice unless the first page names a theme. The choice lasts for
// this run only, as the language choice does (docs/design.md, Persistence).
#pragma once

#include <QColor>
#include <QList>
#include <QPalette>
#include <QString>

namespace gw {

struct Theme
{
    QString id;   // stable; "desktop" follows the desktop
    QString name; // shown in the choice, in the language loaded
    QColor window, base, altBase, text, muted, accent;
    bool highContrast = false;
};

// The id of the theme that follows the desktop.
inline constexpr char kFollowDesktop[] = "desktop";

// Every theme offered, the desktop's first. Its colours are empty.
QList<Theme> themes();

// The palette a theme paints with, its disabled text dimmed.
QPalette paletteFor(const Theme &theme);

// WCAG 2.1 contrast ratio between two colours, from 1 to 21.
double contrast(const QColor &a, const QColor &b);

// Whether the desktop asks for dark: its colour scheme where Qt knows it,
// otherwise the platform's own palette.
bool desktopIsDark();

// Paints the whole application with theme id. "desktop" keeps the
// platform's palette, or the Dark theme where the desktop is dark and the
// platform's palette is light, as in the AppImage. An unknown id follows
// the desktop.
void applyTheme(const QString &id);

// The id last applied, "desktop" before any.
QString currentTheme();

} // namespace gw
