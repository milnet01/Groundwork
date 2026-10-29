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
    bool tooLong = false;  // true: killed when its output passed the cap
    int exitCode = -1;
    QByteArray out;
    QByteArray err;

    bool ok() const { return started && !timedOut && !tooLong && exitCode == 0; }
};

class CommandRunner
{
public:
    static constexpr int DefaultTimeoutMs = 20000;

    // maxOutBytes caps what is kept of standard output; -1 keeps it all.
    CommandResult run(const QStringList &argv, int timeoutMs = DefaultTimeoutMs, qint64 maxOutBytes = -1) const;
};

} // namespace gw
