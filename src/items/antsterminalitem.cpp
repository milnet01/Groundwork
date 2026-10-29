#include "antsterminalitem.h"

#include "packman.h"

#include <QCoreApplication>
#include <QDir>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::AntsTerminalItem", text); }

const QString kPackage = QStringLiteral("ants-terminal");
const QString kRepoPath = QStringLiteral("home:/milnet:/ants-terminal/");
const QString kRepo = QStringLiteral("https://download.opensuse.org/repositories/") + kRepoPath;
const QString kDesktop = QStringLiteral("za.co.antsprojectshub.AntsTerminal.desktop");
const QString kKwriteconfig = QStringLiteral("/usr/bin/kwriteconfig6");

// The repository's folder for this system; empty where it is not built
// (checked 2026-10-10: Slowroll and Leap 16.1 answer 404).
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

// KDE's terminal, from [General] TerminalService in the user's kdeglobals.
QByteArray kdeTerminal(const CheckContext &context)
{
    QString dir = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.config");
    QByteArray service;
    bool general = false;
    for (const QList<QByteArray> lines = context.readFile(dir + QStringLiteral("/kdeglobals")).value_or(QByteArray()).split('\n'); const QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.startsWith('['))
            general = trimmed == "[General]";
        else if (general && trimmed.startsWith("TerminalService="))
            service = trimmed.mid(int(qstrlen("TerminalService="))).trimmed();
    }
    return service;
}
} // namespace

QString AntsTerminalItem::title() const { return tr("Ants Terminal"); }

QString AntsTerminalItem::applySentence() const
{
    return tr("Installs Ants Terminal, a terminal made by Groundwork's author. On KDE Plasma it also becomes the "
              "terminal the desktop opens.");
}

CheckResult AntsTerminalItem::check(const CheckContext &context) const
{
    if (tree(identity(context)).isEmpty())
        return {CheckState::NotNeeded, tr("Ants Terminal is not built for this version of openSUSE.")};
    const CommandResult r = context.run({QStringLiteral("rpm"), QStringLiteral("-q"), kPackage});
    if (!r.started || r.timedOut)
        return {CheckState::CouldNotTell, tr("The installed packages could not be listed.")};
    if (r.exitCode != 0)
        return {CheckState::NotDone, tr("Ants Terminal is not installed.")};
    if (context.fileExists(kKwriteconfig) && kdeTerminal(context) != kDesktop.toUtf8())
        return {CheckState::NotDone, tr("Ants Terminal is not the terminal the desktop opens.")};
    return {CheckState::Done, {}};
}

QList<Step> AntsTerminalItem::applySteps(const SystemIdentity &system, const CheckContext &context) const
{
    const QString label = tr("Installing Ants Terminal");
    QString alias;
    QList<Step> steps = usableRepository(context, kRepoPath, label, &alias);
    if (alias.isEmpty()) {
        alias = kPackage;
        steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("addrepo"), QStringLiteral("-f"),
                       kRepo + tree(system) + QLatin1Char('/'), alias},
                      true, Step::Tool::Zypper, label});
    }
    steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("--gpg-auto-import-keys"),
                   QStringLiteral("refresh"), alias},
                  true, Step::Tool::Zypper, label});
    steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"), kPackage},
                  true, Step::Tool::Zypper, label});
    // The desktop's terminal is the user's own setting, so not root.
    if (context.fileExists(kKwriteconfig)) {
        const QString setLabel = tr("Making Ants Terminal the desktop's terminal");
        const QStringList write = {QStringLiteral("kwriteconfig6"), QStringLiteral("--file"), QStringLiteral("kdeglobals"),
                                   QStringLiteral("--group"), QStringLiteral("General"), QStringLiteral("--key")};
        steps.append({write + QStringList{QStringLiteral("TerminalApplication"), kPackage}, false,
                      Step::Tool::Generic, setLabel});
        steps.append({write + QStringList{QStringLiteral("TerminalService"), kDesktop}, false, Step::Tool::Generic,
                      setLabel});
    }
    return steps;
}

} // namespace gw
