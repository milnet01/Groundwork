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

    // Writes the layout in the file to the drive it names, as one item,
    // prepare-drive (GRND-0076). It trusts nothing in the file.
    int prepareDrive(const QString &layoutPath);

    // Saves the settings and choices in the choices file to one file, as
    // one item, save-setup, with no root step (GRND-0078). It trusts nothing
    // in the choices file.
    int saveSetup(const QString &choicesPath);

private:
    enum class Outcome { Ok, SkippedDone, SkippedNotNeeded, SkippedOther, Failed };

    void openLog();
    void say(const QString &line);
    bool stopRequested() const;
    // Asks for the password the first time a root step is about to run.
    bool ensureRoot();
    // Runs one step; returns false on failure, with a detail line.
    // lines, when given, collects the output; echo off keeps it out of the
    // run's output and log, as a long listing would fill them.
    bool runStep(const QString &itemId, const Step &step, QString *detail, QStringList *lines = nullptr,
                 bool echo = true);
    int runCommand(const QStringList &argv, const QByteArray &input = {}, QStringList *lines = nullptr,
                   bool echo = true);

    const Catalogue &m_catalogue;
    FileReader m_files;
    QString m_stateDir;
    QFile m_log;
    Privilege m_privilege;
    enum class Auth { NotAsked, Ok, Failed } m_auth = Auth::NotAsked;
};

} // namespace gw
