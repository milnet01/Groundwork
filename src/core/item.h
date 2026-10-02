// The interface every setup item implements (docs/design.md, The parts).
// An item never runs a command or opens a file itself: its check asks the
// CheckContext, which Core implements, and its apply returns steps that the
// Worker runs.
#pragma once

#include "commandrunner.h"
#include "filereader.h"
#include "systemidentity.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace gw {

// The design's four levels, in the order the wizard shows them.
enum class Level { Essentials, SystemSetup, Configuration, NiceToHave };

enum class CheckState {
    Done,         // "already done"
    NotDone,      // "not done"
    NotNeeded,    // "not needed here": absent hardware, unavailable package
    CouldNotTell, // "couldn't tell": never shown as done; detail says why
};

struct CheckResult
{
    CheckState state = CheckState::CouldNotTell;
    QString detail; // plain English; required for CouldNotTell
};

// One command the Worker runs for an apply.
struct Step
{
    enum class Tool { Generic, Zypper }; // Zypper: read with zypper's exit-code rule
    QStringList argv;
    bool needsRoot = false;
    Tool tool = Tool::Generic;
    QString label; // plain English, shown on the run page
};

// What a check may ask of Core.
class CheckContext
{
public:
    CheckContext(FileReader files, CommandRunner commands = {})
        : m_files(std::move(files)), m_commands(commands) {}

    std::optional<QByteArray> readFile(const QString &path) const { return m_files.read(path); }
    bool fileExists(const QString &path) const { return m_files.exists(path); }
    QStringList entries(const QString &dir) const { return m_files.entries(dir); }
    QString linkTargetName(const QString &path) const { return m_files.linkTargetName(path); }
    CommandResult run(const QStringList &argv,
                      int timeoutMs = CommandRunner::DefaultTimeoutMs) const
    {
        return m_commands.run(argv, timeoutMs);
    }

private:
    FileReader m_files;
    CommandRunner m_commands;
};

class Item
{
public:
    virtual ~Item() = default;

    virtual QString id() const = 0;      // stable, used on the Worker's command line
    virtual Level level() const = 0;
    virtual QString title() const = 0;   // the row's name
    virtual QString applySentence() const = 0; // S3: what applying would do
    virtual QStringList dependsOn() const { return {}; }
    virtual bool installsPackages() const { return false; }
    // A preparation never starts switched on by itself; every item that
    // installs packages depends on it (docs/design.md, The levels).
    virtual bool isPreparation() const { return false; }

    virtual CheckResult check(const CheckContext &context) const = 0;
    // The steps can differ by system (dup on the rolling family, update on
    // Leap) and by what is already configured, which the context reads
    // without root.
    virtual QList<Step> applySteps(const SystemIdentity &system,
                                   const CheckContext &context) const = 0;
};

} // namespace gw
