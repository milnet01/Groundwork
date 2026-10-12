#include "oneupitem.h"

#include "packman.h"

#include <QCoreApplication>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::OneUpItem", text); }

const QString kPackage = QStringLiteral("oneup");
const QString kAlias = QStringLiteral("home_milnet"); // the alias the README's .repo file gives
const QString kRepoPath = QStringLiteral("home:/milnet/");
const QString kRepo = QStringLiteral("https://download.opensuse.org/repositories/") + kRepoPath;

// The repository's folder for this system; empty where it is not built
// (checked 2026-10-12: Slowroll and Leap 16.1 answer 404).
QString tree(const SystemIdentity &system)
{
    if (system.id == QLatin1String("opensuse-tumbleweed"))
        return QStringLiteral("openSUSE_Tumbleweed");
    if (system.family == SystemIdentity::Family::Leap && system.versionId == QLatin1String("16.0"))
        return QStringLiteral("openSUSE_Leap_16.0");
    return {};
}

SystemIdentity identity(const CheckContext &context)
{
    auto text = context.readFile(QStringLiteral("/etc/os-release"));
    if (!text)
        text = context.readFile(QStringLiteral("/usr/lib/os-release"));
    return parseOsRelease(text.value_or(QByteArray()));
}
} // namespace

QString OneUpItem::title() const { return tr("OneUp updates"); }

QString OneUpItem::applySentence() const
{
    return tr("Installs OneUp, an update app made by Groundwork's author: one window that updates the system, "
              "Flatpak apps and firmware. openSUSE's own updates stay as they are.");
}

CheckResult OneUpItem::check(const CheckContext &context) const
{
    if (tree(identity(context)).isEmpty())
        return {CheckState::NotNeeded, tr("OneUp is not built for this version of openSUSE.")};
    const CommandResult r = context.run({QStringLiteral("rpm"), QStringLiteral("-q"), kPackage});
    if (!r.started || r.timedOut)
        return {CheckState::CouldNotTell, tr("The installed packages could not be listed.")};
    if (r.exitCode != 0)
        return {CheckState::NotDone, tr("OneUp is not installed.")};
    return {CheckState::Done, {}};
}

QList<Step> OneUpItem::applySteps(const SystemIdentity &system, const CheckContext &context) const
{
    const QString label = tr("Installing OneUp");
    QList<Step> steps;
    QString alias = repositoryAlias(context, kRepoPath);
    if (alias.isEmpty()) {
        alias = kAlias;
        steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("addrepo"), QStringLiteral("-f"),
                       kRepo + tree(system) + QLatin1Char('/'), alias},
                      true, Step::Tool::Zypper, label});
    }
    steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("--gpg-auto-import-keys"),
                   QStringLiteral("refresh"), alias},
                  true, Step::Tool::Zypper, label});
    steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"), kPackage},
                  true, Step::Tool::Zypper, label});
    return steps;
}

} // namespace gw
