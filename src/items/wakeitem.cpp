#include "wakeitem.h"

#include <QCoreApplication>
#include <QMap>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::WakeItem", text); }

const QString kVendor = QStringLiteral("/usr/lib/systemd/system/");
const QString kAdmin = QStringLiteral("/etc/systemd/system/");
const QString kDropIn = QStringLiteral("99-groundwork-spread.conf");

// The daily and hourly maintenance timers that pile up after a wake, as
// seen on the owner's machine.
const QStringList kTimers = {
    QStringLiteral("backup-rpmdb"),     QStringLiteral("backup-sysconfig"), QStringLiteral("check-battery"),
    QStringLiteral("logrotate"),        QStringLiteral("packagekit-background"),
    QStringLiteral("snapper-cleanup"),  QStringLiteral("snapper-timeline"), QStringLiteral("unbound-anchor"),
};

// The unit file the system uses: one in /etc replaces the distribution's.
QString unitFile(const CheckContext &context, const QString &timer)
{
    const QString name = timer + QStringLiteral(".timer");
    if (context.fileExists(kAdmin + name))
        return kAdmin + name;
    if (context.fileExists(kVendor + name))
        return kVendor + name;
    return {};
}

// systemd reads the unit, then its drop-ins in file-name order; one in
// /etc replaces a drop-in of the same name elsewhere. The last setting wins.
bool hasDelay(const CheckContext &context, const QString &unit, const QString &timer)
{
    QStringList files = {unit};
    QMap<QString, QString> dropIns;
    const QString dir = timer + QStringLiteral(".timer.d/");
    for (const QString &base : {kVendor, kAdmin})
        for (const QString &name : context.entries(base + dir))
            if (name.endsWith(QLatin1String(".conf")))
                dropIns.insert(name, base + dir + name);
    files += dropIns.values();

    QByteArray delay;
    for (const QString &file : files)
        for (const QByteArray &line : context.readFile(file).value_or(QByteArray()).split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.startsWith("RandomizedDelaySec="))
                delay = trimmed.mid(int(qstrlen("RandomizedDelaySec="))).trimmed();
        }
    return !delay.isEmpty() && delay != "0";
}

QStringList timersWithoutDelay(const CheckContext &context, bool *anyFound)
{
    QStringList missing;
    for (const QString &timer : kTimers) {
        const QString unit = unitFile(context, timer);
        if (unit.isEmpty())
            continue;
        *anyFound = true;
        if (!hasDelay(context, unit, timer))
            missing << timer;
    }
    return missing;
}
} // namespace

QString WakeItem::title() const { return tr("Calmer wake from hibernation"); }

QString WakeItem::applySentence() const
{
    return tr("Spreads out the maintenance jobs that were missed while the computer slept, so they "
              "no longer all start the moment it wakes.");
}

CheckResult WakeItem::check(const CheckContext &context) const
{
    bool found = false;
    const QStringList missing = timersWithoutDelay(context, &found);
    if (!found)
        return {CheckState::NotNeeded, tr("None of the maintenance jobs this spreads out were found.")};
    if (missing.isEmpty())
        return {CheckState::Done, {}};
    return {CheckState::NotDone, tr("Some maintenance jobs all start at once after a wake.")};
}

QList<Step> WakeItem::applySteps(const SystemIdentity &, const CheckContext &context) const
{
    bool found = false;
    const QStringList missing = timersWithoutDelay(context, &found);
    const QString label = tr("Spreading out the maintenance jobs");
    QStringList dirs;
    for (const QString &timer : missing)
        dirs << kAdmin + timer + QStringLiteral(".timer.d");
    QList<Step> steps = {makeDirectoriesStep(dirs, label)};
    for (const QString &dir : dirs)
        steps << writeFileStep(dir + QLatin1Char('/') + kDropIn,
                               "# Written by Groundwork: a calmer wake from hibernation. Remove this file to undo.\n"
                               "[Timer]\nRandomizedDelaySec=30min\n",
                               label);
    steps << Step{{QStringLiteral("systemctl"), QStringLiteral("daemon-reload")}, true, Step::Tool::Generic, label};
    return steps;
}

} // namespace gw
