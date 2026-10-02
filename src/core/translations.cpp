#include "translations.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

#include <memory>
#include <vector>

namespace gw {
namespace {

const QString kResourceDir = QStringLiteral(":/i18n");

class PseudoTranslator : public QTranslator
{
public:
    QString translate(const char *, const char *sourceText, const char *, int) const override
    {
        return QStringLiteral("⟦") + QString::fromUtf8(sourceText) + QStringLiteral("⟧");
    }
    bool isEmpty() const override { return false; }
};

std::vector<std::unique_ptr<QTranslator>> &installed()
{
    static std::vector<std::unique_ptr<QTranslator>> list;
    return list;
}

} // namespace

QStringList availableLanguages()
{
    QStringList codes{QStringLiteral("en")};
    const QStringList files = QDir(kResourceDir).entryList({QStringLiteral("groundwork_*.qm")}, QDir::Files);
    for (const QString &file : files)
        codes << file.mid(11, file.size() - 11 - 3); // groundwork_<code>.qm
    return codes;
}

QString systemLanguage()
{
    const QStringList have = availableLanguages();
    const QString name = QLocale::system().name(); // e.g. af_ZA
    if (have.contains(name))
        return name;
    const QString language = name.section(QLatin1Char('_'), 0, 0);
    return have.contains(language) ? language : QStringLiteral("en");
}

bool loadLanguage(const QString &code)
{
    for (auto &t : installed())
        QCoreApplication::removeTranslator(t.get());
    installed().clear();

    if (code == kPseudoLanguage) {
        installed().push_back(std::make_unique<PseudoTranslator>());
        QCoreApplication::installTranslator(installed().back().get());
        return true;
    }
    if (code == QLatin1String("en"))
        return true;

    auto own = std::make_unique<QTranslator>();
    if (!own->load(QStringLiteral("groundwork_") + code, kResourceDir))
        return false;
    QCoreApplication::installTranslator(own.get());
    installed().push_back(std::move(own));
    // Qt's own words, such as standard button labels.
    auto qt = std::make_unique<QTranslator>();
    if (qt->load(QStringLiteral("qtbase_") + code, QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(qt.get());
        installed().push_back(std::move(qt));
    }
    return true;
}

Qt::LayoutDirection directionFor(const QString &code)
{
    if (code == kPseudoLanguage)
        return Qt::LeftToRight;
    return QLocale(code).textDirection();
}

} // namespace gw
