#include "packman.h"

#include <QCoreApplication>

namespace gw {
namespace {

const QString kReposDir = QStringLiteral("/etc/zypp/repos.d");
const QString kEssentialsAlias = QStringLiteral("packman-essentials");
const QString kMirror = QStringLiteral("https://ftp.gwdg.de/pub/linux/misc/packman/suse/");

} // namespace

QString repositoryAlias(const CheckContext &context, const QString &urlPart)
{
    for (const QString &name : context.entries(kReposDir)) {
        if (!name.endsWith(QLatin1String(".repo")))
            continue;
        const auto text = context.readFile(kReposDir + QLatin1Char('/') + name);
        if (!text)
            continue;
        // One .repo file can hold several [alias] sections.
        QString alias;
        bool matches = false;
        bool enabled = true;
        auto finish = [&]() { return matches && enabled ? alias : QString(); };
        for (const QByteArray &raw : text->split('\n')) {
            const QString line = QString::fromUtf8(raw).trimmed();
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                if (!finish().isEmpty())
                    return finish();
                alias = line.mid(1, line.size() - 2);
                matches = false;
                enabled = true;
            } else if (line.startsWith(QLatin1String("baseurl="))) {
                matches = line.contains(urlPart, Qt::CaseInsensitive);
            } else if (line.startsWith(QLatin1String("enabled="))) {
                enabled = line.mid(8).trimmed() == QLatin1String("1");
            }
        }
        if (!finish().isEmpty())
            return finish();
    }
    return {};
}

QString packmanAlias(const CheckContext &context)
{
    return repositoryAlias(context, QStringLiteral("packman"));
}

QList<Step> packmanSteps(const SystemIdentity &system, const CheckContext &context, QString *alias)
{
    QList<Step> steps;
    *alias = packmanAlias(context);
    if (alias->isEmpty()) {
        *alias = kEssentialsAlias;
        QString tree;
        if (system.id == QLatin1String("opensuse-slowroll"))
            tree = QStringLiteral("openSUSE_Slowroll");
        else if (system.family == SystemIdentity::Family::Leap)
            tree = QStringLiteral("openSUSE_Leap_$releasever"); // zypper expands it
        else
            tree = QStringLiteral("openSUSE_Tumbleweed");
        steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("addrepo"),
                       QStringLiteral("-cfp"), QStringLiteral("90"),
                       kMirror + tree + QStringLiteral("/Essentials/"), *alias},
                      true, Step::Tool::Zypper, QCoreApplication::translate("gw::Packman", "Adding the Packman Essentials software source")});
    }
    steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"),
                   QStringLiteral("--gpg-auto-import-keys"), QStringLiteral("refresh"), *alias},
                  true, Step::Tool::Zypper, QCoreApplication::translate("gw::Packman", "Reading Packman's software list")});
    return steps;
}

} // namespace gw
