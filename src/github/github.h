// GitHub (GRND-0065; docs/specs/GRND-0065-copy-github-projects.md): reading
// the list of the user's repositories, and the steps that copy them. Like
// Items, it names commands and runs nothing itself (docs/design.md, What may
// depend on what). It never asks gh for the token (INV-1).
#pragma once

#include "core/item.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace gw {

// gh repo list's --limit; a list this long may have been cut short.
inline constexpr int kGithubListLimit = 1000;

struct Repository
{
    QString nameWithOwner; // passes isRepositoryName
    bool isPrivate = false;
    bool isFork = false;
    qint64 diskKiB = 0; // GitHub's figure; a checkout can differ from it

    QString name() const { return nameWithOwner.section(QLatin1Char('/'), 1); }
    bool startsOn() const { return !isFork; } // INV-2
};

// Whether a name may reach a command (spec 4.4, INV-3).
bool isRepositoryName(const QString &text);

// The Wizard's arguments to gh for signing in (spec 4.3).
QStringList signInArguments();
// The one-time code in what gh wrote to standard error, or empty (spec 4.3).
QString oneTimeCode(const QByteArray &ghErr);
// What gh said after a sign-in, leaving out the code and the address that
// go with it: why it failed, and any warning when it did not.
QString signInWords(const QByteArray &ghErr);

// Keeps the entries whose names pass; nullopt when the output is not a list.
std::optional<QList<Repository>> parseRepositories(const QByteArray &ghOut);

// What the job's pages read as the user, with nothing changed. Each part is
// read only once the one before it allows.
struct GithubSurvey
{
    QStringList missing; // of git and gh
    bool signedIn = false;
    QString account;
    QList<Repository> repositories;
    bool listDamaged = false;
};
GithubSurvey surveyGithub(const CheckContext &context);

// A sentence saying why the folder cannot hold the copies, or empty (spec
// 4.6). Asked by the page and by the Worker.
QString githubFolderProblem(const QString &folder);
// The folder itself if it exists, or its nearest parent that does.
QString nearestExistingFolder(const QString &folder);

// What the Wizard hands the Worker (spec 4.5).
struct GithubChoices
{
    QString folder;
    QStringList repos;
};
QByteArray githubChoicesJson(const GithubChoices &choices);
// Trusts nothing: nullopt, with a sentence, unless the folder and every name
// pass and no two names share a last part (INV-3).
std::optional<GithubChoices> readGithubChoices(const QByteArray &json, QString *why);

// The steps. Only the first needs root (INV-7).
Step githubToolsStep();
Step makeGithubFolderStep(const QString &folder);
Step setupGitStep();
// Where one project goes, and where it is copied first.
QString githubDestination(const QString &folder, const QString &nameWithOwner);
QString githubPartial(const QString &folder, const QString &nameWithOwner);
// Clone beside the destination, then move it there, never over anything.
QList<Step> githubCopySteps(const QString &folder, const QString &nameWithOwner);

} // namespace gw
