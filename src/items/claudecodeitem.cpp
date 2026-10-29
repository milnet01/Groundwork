#include "claudecodeitem.h"

#include "packman.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::ClaudeCodeItem", text); }

const QString kPackage = QStringLiteral("claude-code");
const QString kRepoPath = QStringLiteral("downloads.claude.ai/claude-code/rpm/stable");
const QString kRepo = QStringLiteral("https://") + kRepoPath;
// rpm cannot import a key from a pipe (measured 2026-10-26: "import read
// failed"), so the key is written to a file first.
const QString kKeyFile = QStringLiteral("/etc/pki/rpm-gpg/claude-code.asc");

// Anthropic's signing key is built in at :/items, so no downloaded key is
// trusted. Its fingerprint, checked against the one in the roadmap item
// (2026-10-26): 31DDDE24DDFAB679F42D7BD2BAA929FF1A7ECACE.
QByteArray key()
{
    QFile f(QStringLiteral(":/items/claude-code.asc"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
} // namespace

QString ClaudeCodeItem::title() const { return tr("Claude Code"); }

QString ClaudeCodeItem::applySentence() const
{
    return tr("Installs Claude Code, Anthropic's coding assistant, from Anthropic's own repository, so system "
              "updates keep it up to date. Using it needs a paid Claude plan.");
}

CheckResult ClaudeCodeItem::check(const CheckContext &context) const
{
    // On PATH, or in ~/.local/bin, where Anthropic's own installer puts it.
    QStringList dirs = qEnvironmentVariable("PATH").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    dirs.append(QDir::homePath() + QStringLiteral("/.local/bin"));
    if (std::any_of(dirs.cbegin(), dirs.cend(),
                    [&](const QString &dir) { return context.fileExists(dir + QStringLiteral("/claude")); }))
        return {CheckState::Done, {}};
    return {CheckState::NotDone, tr("Claude Code is not installed.")};
}

QList<Step> ClaudeCodeItem::applySteps(const SystemIdentity &, const CheckContext &context) const
{
    const QString label = tr("Installing Claude Code");
    const QByteArray builtInKey = key();
    // Compiled in, so only a broken build lacks it; then say what failed.
    if (builtInKey.isEmpty())
        return {Step{{QStringLiteral("false")}, false, Step::Tool::Generic,
                     tr("Reading Groundwork's built-in signing key for Claude Code")}};
    QList<Step> steps = {
        makeDirectoriesStep({QFileInfo(kKeyFile).path()}, label),
        writeFileStep(kKeyFile, builtInKey, label),
        {{QStringLiteral("rpm"), QStringLiteral("--import"), kKeyFile}, true, Step::Tool::Generic, label},
    };
    QString alias;
    steps << usableRepository(context, kRepoPath, label, &alias);
    if (alias.isEmpty()) {
        alias = kPackage;
        steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("addrepo"), QStringLiteral("-f"),
                       kRepo, alias},
                      true, Step::Tool::Zypper, label});
    }
    steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("refresh"), alias},
                  true, Step::Tool::Zypper, label});
    steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"), kPackage},
                  true, Step::Tool::Zypper, label});
    return steps;
}

} // namespace gw
