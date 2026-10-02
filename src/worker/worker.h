// The Worker (docs/design.md; its interface is
// docs/reference/worker-interface.md): runs the given items in catalogue
// order, re-checking each before applying it, and reports progress as
// marker lines on standard output and in a log file.
#pragma once

#include "core/filereader.h"
#include "core/item.h"
#include "core/selection.h"
#include "privilege.h"

#include <QFile>
#include <QHash>
#include <QStringList>

namespace gw {

class Worker
{
public:
    enum Exit { Ok = 0, Failed = 1, Usage = 2, Unsupported = 3, AuthFailed = 4, Stopped = 5 };

    Worker(const Catalogue &catalogue, FileReader files, QString stateDir);

    // values: what the user gave items that take one, keyed by item id.
    int run(const QStringList &ids, const QHash<QString, QString> &values = {});

private:
    enum class Outcome { Ok, SkippedDone, SkippedNotNeeded, SkippedOther, Failed };

    void say(const QString &line);
    bool stopRequested() const;
    // Asks for the password the first time a root step is about to run.
    bool ensureRoot();
    // Runs one step; returns false on failure, with a detail line.
    bool runStep(const QString &itemId, const Step &step, QString *detail);
    int runCommand(const QStringList &argv);

    const Catalogue &m_catalogue;
    FileReader m_files;
    QString m_stateDir;
    QFile m_log;
    Privilege m_privilege;
    enum class Auth { NotAsked, Ok, Failed } m_auth = Auth::NotAsked;
};

} // namespace gw
