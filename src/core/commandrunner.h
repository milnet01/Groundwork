// Runs one read-only command for a check: a fixed argument list, never a
// shell, with a time limit. Commands are found on PATH, so tests put fakes
// first on PATH (docs/design.md, What may depend on what).
#pragma once

#include <QByteArray>
#include <QStringList>

namespace gw {

struct CommandResult
{
    bool started = false;  // false: the program was not found or could not run
    bool timedOut = false; // true: killed at the time limit
    int exitCode = -1;
    QByteArray out;
    QByteArray err;

    bool ok() const { return started && !timedOut && exitCode == 0; }
};

class CommandRunner
{
public:
    static constexpr int DefaultTimeoutMs = 20000;

    CommandResult run(const QStringList &argv, int timeoutMs = DefaultTimeoutMs) const;
};

} // namespace gw
