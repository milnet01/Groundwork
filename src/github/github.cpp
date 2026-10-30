#include "github.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace gw {
namespace {

bool isAtOrBelow(const QString &path, const QString &dir)
{
    return path == dir || path.startsWith(dir + QLatin1Char('/'));
}

} // namespace

bool isRepositoryName(const QString &text)
{
    // The spec's pattern, with an owner that cannot begin with - or a dot:
    // the name comes first on gh's line, so -x/y would read as an option.
    static const QRegularExpression pattern(
        QStringLiteral(R"(\A[A-Za-z0-9_][A-Za-z0-9_.-]*/(?!\.{1,2}\z)[A-Za-z0-9_.][A-Za-z0-9_.-]*\z)"));
    return pattern.match(text).hasMatch();
}

QStringList signInArguments()
{
    return {QStringLiteral("auth"), QStringLiteral("login"), QStringLiteral("--hostname"), QStringLiteral("github.com"),
            QStringLiteral("--git-protocol"), QStringLiteral("https"), QStringLiteral("--web")};
}

QString oneTimeCode(const QByteArray &ghErr)
{
    static const QRegularExpression pattern(QStringLiteral(R"(\b[A-Z0-9]{4}-[A-Z0-9]{4}\b)"));
    return pattern.match(QString::fromUtf8(ghErr)).captured();
}

QString signInWords(const QByteArray &ghErr)
{
    QStringList kept;
    for (const QString &line : QString::fromUtf8(ghErr).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (oneTimeCode(line.toUtf8()).isEmpty() && !line.contains(QLatin1String("github.com/login/device")))
            kept << line.trimmed();
    }
    return kept.join(QLatin1Char('\n'));
}

std::optional<QList<Repository>> parseRepositories(const QByteArray &ghOut)
{
    const QJsonDocument document = QJsonDocument::fromJson(ghOut);
    if (!document.isArray())
        return std::nullopt;
    QList<Repository> repositories;
    for (const QJsonArray entries = document.array(); const QJsonValue &entry : entries) {
        const QJsonObject object = entry.toObject();
        const QString name = object.value("nameWithOwner").toString();
        if (isRepositoryName(name))
            repositories.append({name, object.value("isPrivate").toBool(), object.value("isFork").toBool(),
                                 qMax(qint64(0), qint64(object.value("diskUsage").toDouble()))});
    }
    return repositories;
}

GithubSurvey surveyGithub(const CheckContext &context)
{
    GithubSurvey survey;
    // The format, so this holds in any language: rpm's words for a missing
    // package are translated.
    const QStringList found = QString::fromUtf8(context.run({QStringLiteral("rpm"), QStringLiteral("-q"),
                                                             QStringLiteral("--qf"), QStringLiteral("%{NAME}\n"),
                                                             QStringLiteral("git"), QStringLiteral("gh")})
                                                    .out)
                                  .split(QLatin1Char('\n'));
    for (const QString &program : {QStringLiteral("git"), QStringLiteral("gh")}) {
        if (!found.contains(program))
            survey.missing << program;
    }
    if (!survey.missing.isEmpty())
        return survey;
    survey.signedIn = context.run({QStringLiteral("gh"), QStringLiteral("auth"), QStringLiteral("status")}).ok();
    if (!survey.signedIn)
        return survey;
    const CommandResult user = context.run({QStringLiteral("gh"), QStringLiteral("api"), QStringLiteral("user"),
                                            QStringLiteral("--jq"), QStringLiteral(".login")});
    if (user.ok())
        survey.account = QString::fromUtf8(user.out).trimmed();
    const CommandResult list = context.run({QStringLiteral("gh"), QStringLiteral("repo"), QStringLiteral("list"),
                                            QStringLiteral("--limit"), QString::number(kGithubListLimit),
                                            QStringLiteral("--json"),
                                            QStringLiteral("nameWithOwner,isPrivate,isFork,diskUsage")},
                                           60000);
    const std::optional<QList<Repository>> repositories = list.ok() ? parseRepositories(list.out) : std::nullopt;
    survey.listDamaged = !repositories;
    survey.repositories = repositories.value_or(QList<Repository>{});
    return survey;
}

QString nearestExistingFolder(const QString &folder)
{
    QFileInfo info(folder);
    while (!info.exists() && !info.isRoot() && info.filePath() != info.path())
        info.setFile(info.path());
    return info.filePath();
}

QString githubFolderProblem(const QString &folder)
{
    if (!QDir::isAbsolutePath(folder) || folder != QDir::cleanPath(folder))
        return QCoreApplication::translate("gw::GitHub", "This folder's path could not be used.");
    // The system folders of GRND-0066 spec 4.3.
    if (folder == QLatin1String("/"))
        return QCoreApplication::translate("gw::GitHub", "This folder holds system files. Choose another.");
    for (const char *dir : {"/bin", "/boot", "/dev", "/etc", "/lib", "/lib64", "/opt", "/proc", "/root", "/sbin", "/sys",
                            "/usr", "/var"}) {
        if (isAtOrBelow(folder, QLatin1String(dir)))
            return QCoreApplication::translate("gw::GitHub", "This folder holds system files. Choose another.");
    }
    if (isAtOrBelow(folder, QStringLiteral("/run")) && !isAtOrBelow(folder, QStringLiteral("/run/media")))
        return QCoreApplication::translate("gw::GitHub", "This folder holds system files. Choose another.");
    const QString home = QDir::homePath();
    if (isAtOrBelow(folder, home)) {
        for (const QString &part : folder.mid(home.size()).split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
            if (part.startsWith(QLatin1Char('.')))
                return QCoreApplication::translate("gw::GitHub", "Hidden folders in your home folder hold settings. Choose another.");
        }
    }
    const QFileInfo existing(nearestExistingFolder(folder));
    if (!existing.isDir())
        return QCoreApplication::translate("gw::GitHub", "%1 is a file, not a folder.").arg(existing.filePath());
    if (!existing.isWritable())
        return QCoreApplication::translate("gw::GitHub", "You cannot write to %1. Choose another folder.").arg(existing.filePath());
    return {};
}

QByteArray githubChoicesJson(const GithubChoices &choices)
{
    return QJsonDocument(QJsonObject{{"folder", choices.folder}, {"repos", QJsonArray::fromStringList(choices.repos)}})
        .toJson();
}

std::optional<GithubChoices> readGithubChoices(const QByteArray &json, QString *why)
{
    const QJsonObject object = QJsonDocument::fromJson(json).object();
    GithubChoices choices;
    choices.folder = object.value("folder").toString();
    if (const QString problem = githubFolderProblem(choices.folder); !problem.isEmpty()) {
        *why = problem;
        return std::nullopt;
    }
    QSet<QString> lastParts;
    for (const QJsonArray repos = object.value("repos").toArray(); const QJsonValue &repo : repos) {
        const QString name = repo.toString();
        if (!isRepositoryName(name)) {
            *why = QCoreApplication::translate("gw::GitHub", "The list of projects holds a name that cannot be used.");
            return std::nullopt;
        }
        // Each goes to a folder named by its last part.
        const QString last = name.section(QLatin1Char('/'), 1);
        if (lastParts.contains(last)) {
            *why = QCoreApplication::translate("gw::GitHub", "Two projects would go to the same folder: %1.").arg(last);
            return std::nullopt;
        }
        lastParts.insert(last);
        choices.repos << name;
    }
    if (choices.repos.isEmpty()) {
        *why = QCoreApplication::translate("gw::GitHub", "The list of projects to copy is empty.");
        return std::nullopt;
    }
    return choices;
}

Step githubToolsStep()
{
    return {{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"), QStringLiteral("git"), QStringLiteral("gh")},
            true, Step::Tool::Zypper, QCoreApplication::translate("gw::GitHub", "Installing git and gh")};
}

Step makeGithubFolderStep(const QString &folder)
{
    return {{QStringLiteral("mkdir"), QStringLiteral("-p"), QStringLiteral("--"), folder}, false, Step::Tool::Generic,
            QCoreApplication::translate("gw::GitHub", "Making the folder")};
}

Step setupGitStep()
{
    return {{QStringLiteral("gh"), QStringLiteral("auth"), QStringLiteral("setup-git")}, false, Step::Tool::Generic,
            QCoreApplication::translate("gw::GitHub", "Letting git use your sign-in")};
}

QString githubDestination(const QString &folder, const QString &nameWithOwner)
{
    return folder + QLatin1Char('/') + nameWithOwner.section(QLatin1Char('/'), 1);
}

QString githubPartial(const QString &folder, const QString &nameWithOwner)
{
    return folder + QStringLiteral("/.") + nameWithOwner.section(QLatin1Char('/'), 1) + QStringLiteral(".partial");
}

QList<Step> githubCopySteps(const QString &folder, const QString &nameWithOwner)
{
    const QString partial = githubPartial(folder, nameWithOwner);
    // The env is why a revoked sign-in fails instead of waiting for a
    // password: KDE sets SSH_ASKPASS, and git would open its box for each
    // project (INV-6). -T: a folder that has appeared at the destination is
    // not moved into (INV-4).
    return {{{QStringLiteral("env"), QStringLiteral("GIT_TERMINAL_PROMPT=0"), QStringLiteral("GIT_ASKPASS="),
              QStringLiteral("SSH_ASKPASS="), QStringLiteral("gh"), QStringLiteral("repo"), QStringLiteral("clone"),
              nameWithOwner, partial},
             false, Step::Tool::Generic, QCoreApplication::translate("gw::GitHub", "Copying the project")},
            {{QStringLiteral("mv"), QStringLiteral("-n"), QStringLiteral("-T"), QStringLiteral("--"), partial,
              githubDestination(folder, nameWithOwner)},
             false, Step::Tool::Generic, QCoreApplication::translate("gw::GitHub", "Putting the project in place")}};
}

} // namespace gw
