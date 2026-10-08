// The language the app speaks (GRND-0032; docs/design.md, Text). The
// language follows the system's unless one is chosen; a right-to-left
// language mirrors the layout. Commands a check reads still run with
// LC_ALL=C (commandrunner.cpp), whatever this says.
#pragma once

#include <QString>
#include <QStringList>
#include <Qt>

namespace gw {

// "pseudo" wraps every translatable string in ⟦ ⟧, so a string that is
// not translatable shows up unwrapped.
inline const QString kPseudoLanguage = QStringLiteral("pseudo");

// Language codes with a non-empty translation built in, plus "en", the
// source.
QStringList availableLanguages();

// The system's language as a code this app has, else "en".
QString systemLanguage();

// Installs the translation for a code, replacing any installed before.
// Returns false, leaving the source text, when there is none.
bool loadLanguage(const QString &code);

// True for a translation no native speaker has checked yet, which ships
// marked as a draft (GRND-0041; docs/design.md, Text). False for English.
bool isDraft(const QString &code);

Qt::LayoutDirection directionFor(const QString &code);

} // namespace gw
