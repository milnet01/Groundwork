#include "packman.h"

#include <QCoreApplication>

namespace gw {
namespace {

const QString kReposDir = QStringLiteral("/etc/zypp/repos.d");
const QString kAlias = QStringLiteral("packman");
const QString kMirror = QStringLiteral("https://ftp.gwdg.de/pub/linux/misc/packman/suse/");

// The alias of the first enabled repository whose baseurl line passes
// matches; empty when there is none. With disabled set, a disabled one
// counts too, after any enabled one, and *disabled says which was found.
template<typename Matches>
QString aliasWhere(const CheckContext &context, Matches baseurlMatches, bool *disabled = nullptr)
{
    QString firstDisabled;
    for (const QStringList names = context.entries(kReposDir); const QString &name : names) {
        if (!name.endsWith(QLatin1String(".repo")))
            continue;
        const auto text = context.readFile(kReposDir + QLatin1Char('/') + name);
        if (!text)
            continue;
        // One .repo file can hold several [alias] sections.
        QString alias;
        bool matches = false;
        bool enabled = true;
        auto finish = [&]() {
            if (disabled && matches && !enabled && firstDisabled.isEmpty())
                firstDisabled = alias;
            return matches && enabled ? alias : QString();
        };
        for (const QList<QByteArray> rawLines = text->split('\n'); const QByteArray &raw : rawLines) {
            const QString line = QString::fromUtf8(raw).trimmed();
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                if (!finish().isEmpty())
                    return finish();
                alias = line.mid(1, line.size() - 2);
                matches = false;
                enabled = true;
            } else if (line.startsWith(QLatin1String("baseurl="))) {
                matches = baseurlMatches(line);
            } else if (line.startsWith(QLatin1String("enabled="))) {
                enabled = line.mid(8).trimmed() == QLatin1String("1");
            }
        }
        if (!finish().isEmpty())
            return finish();
    }
    if (disabled)
        *disabled = !firstDisabled.isEmpty();
    return firstDisabled;
}

// Switches on the repository a disabled match names, so adding it again
// never fails on the alias it already holds (GRND-0093).
QList<Step> enabling(const QString &alias, bool disabled, const QString &label)
{
    if (!disabled)
        return {};
    return {Step{{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("modifyrepo"), QStringLiteral("-e"), alias},
                 true, Step::Tool::Zypper, label}};
}

bool isFullPackman(const QString &line)
{
    return line.contains(QLatin1String("packman"), Qt::CaseInsensitive)
        && !line.contains(QLatin1String("/Essentials"), Qt::CaseInsensitive);
}

} // namespace

QList<Step> usableRepository(const CheckContext &context, const QString &urlPart, const QString &label, QString *alias)
{
    bool disabled = false;
    *alias = aliasWhere(context, [&](const QString &line) { return line.contains(urlPart, Qt::CaseInsensitive); },
                        &disabled);
    return enabling(*alias, disabled, label);
}

QString packmanAlias(const CheckContext &context) { return aliasWhere(context, isFullPackman); }

QList<Step> packmanSteps(const SystemIdentity &system, const CheckContext &context, QString *alias)
{
    bool disabled = false;
    *alias = aliasWhere(context, isFullPackman, &disabled);
    QList<Step> steps =
        enabling(*alias, disabled, QCoreApplication::translate("gw::Packman", "Switching on the Packman software source"));
    if (alias->isEmpty()) {
        *alias = kAlias;
        QString tree;
        if (system.id == QLatin1String("opensuse-slowroll"))
            tree = QStringLiteral("openSUSE_Slowroll");
        else if (system.family == SystemIdentity::Family::Leap)
            tree = QStringLiteral("openSUSE_Leap_$releasever"); // zypper expands it
        else
            tree = QStringLiteral("openSUSE_Tumbleweed");
        steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("addrepo"),
                       QStringLiteral("-cfp"), QStringLiteral("90"),
                       kMirror + tree + QLatin1Char('/'), *alias},
                      true, Step::Tool::Zypper, QCoreApplication::translate("gw::Packman", "Adding the Packman software source")});
    }
    steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"),
                   QStringLiteral("--gpg-auto-import-keys"), QStringLiteral("refresh"), *alias},
                  true, Step::Tool::Zypper, QCoreApplication::translate("gw::Packman", "Reading Packman's software list")});
    return steps;
}

} // namespace gw
