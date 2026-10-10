#include "memoryitem.h"

#include "service.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::MemoryItem", text); }

const QString kUnit = QStringLiteral("systemd-oomd");
const QString kFile = QStringLiteral("/50-groundwork-memory.conf");
const QByteArray kHeader = "# Written by Groundwork: RAM protection. Remove this file to undo.\n";

const QString kUserService = QStringLiteral("/etc/systemd/system/user@.service.d");
const QString kUserSlice = QStringLiteral("/etc/systemd/system/user.slice.d");
const QString kUserNSlice = QStringLiteral("/etc/systemd/system/user-.slice.d");
const QString kRootSlice = QStringLiteral("/etc/systemd/system/-.slice.d");
const QString kSession = QStringLiteral("/etc/systemd/user/session.slice.d");
const QString kApps = QStringLiteral("/etc/systemd/user/app.slice.d");
const QString kOomd = QStringLiteral("/etc/systemd/oomd.conf.d");

// Whether any .conf file in dir sets key to a value matching pattern. The
// file's name is not checked, so protection set up by hand counts too.
bool anySets(const CheckContext &context, const QString &dir, const QString &key, const QString &pattern)
{
    const QRegularExpression line(QStringLiteral("^\\s*%1\\s*=\\s*%2\\s*$").arg(key, pattern),
                                  QRegularExpression::MultilineOption);
    for (const QString &name : context.entries(dir)) {
        if (!name.endsWith(QLatin1String(".conf")))
            continue;
        if (const auto text = context.readFile(dir + QLatin1Char('/') + name);
            text && line.match(QString::fromUtf8(*text)).hasMatch())
            return true;
    }
    return false;
}

qint64 totalMemoryKiB(const CheckContext &context)
{
    static const QRegularExpression total(QStringLiteral("^MemTotal:\\s+(\\d+) kB"),
                                          QRegularExpression::MultilineOption);
    const auto text = context.readFile(QStringLiteral("/proc/meminfo"));
    const auto m = text ? total.match(QString::fromUtf8(*text)) : QRegularExpressionMatch();
    return m.hasMatch() ? m.captured(1).toLongLong() : 0;
}
} // namespace

QString MemoryItem::floorFor(qint64 totalKiB)
{
    constexpr qint64 GiB = 1024 * 1024;
    if (totalKiB >= 8 * GiB)
        return QStringLiteral("2G");
    if (totalKiB >= 4 * GiB)
        return QStringLiteral("1G");
    return QStringLiteral("512M");
}

QString MemoryItem::title() const { return tr("RAM protection"); }

QString MemoryItem::applySentence() const
{
    return tr("When memory runs short, keeps the desktop working and closes the app using the most "
              "memory, instead of letting the whole computer freeze. Takes full effect after you "
              "next log in.");
}

CheckResult MemoryItem::check(const CheckContext &context) const
{
    const ServiceState s = readService(context, kUnit);
    if (!s.known)
        return {CheckState::CouldNotTell, tr("The service manager did not answer.")};
    const bool watching = s.found && s.enabled && s.active;
    const bool shared = anySets(context, kUserService, QStringLiteral("Delegate"), QStringLiteral(".*\\bmemory\\b.*"));
    const bool appsKillable = anySets(context, kApps, QStringLiteral("ManagedOOMMemoryPressure"), QStringLiteral("kill"));
    if (watching && shared && appsKillable)
        return {CheckState::Done, {}};
    if (!s.found)
        return {CheckState::NotDone, tr("The memory watchdog, systemd-oomd, is not installed.")};
    return {CheckState::NotDone, tr("Nothing closes an app before memory runs out.")};
}

QList<Step> MemoryItem::applySteps(const SystemIdentity &, const CheckContext &context) const
{
    const QByteArray floor = floorFor(totalMemoryKiB(context)).toLatin1();
    const QString label = tr("Writing the RAM protection settings");
    QList<Step> steps;
    if (!readService(context, kUnit).found)
        steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"),
                       QStringLiteral("systemd-experimental")},
                      true, Step::Tool::Zypper, tr("Installing the memory watchdog")});
    steps.append(makeDirectoriesStep({kUserService, kUserSlice, kUserNSlice, kRootSlice, kSession, kApps, kOomd},
                                     label));
    // The floor is repeated down the chain because a child's MemoryMin is
    // capped by its parents' (systemd.resource-control, MemoryMin=).
    steps.append(writeFileStep(kUserService + kFile,
                               kHeader + "[Service]\nDelegate=pids memory cpu io\nMemoryAccounting=yes\n"
                                         "CPUAccounting=yes\nIOAccounting=yes\nMemoryMin=" + floor + "\n",
                               label));
    steps.append(writeFileStep(kUserSlice + kFile,
                               kHeader + "[Slice]\nMemoryAccounting=yes\nCPUAccounting=yes\nIOAccounting=yes\n"
                                         "MemoryMin=" + floor + "\n",
                               label));
    steps.append(writeFileStep(kUserNSlice + kFile, kHeader + "[Slice]\nMemoryMin=" + floor + "\n", label));
    steps.append(writeFileStep(kRootSlice + kFile, kHeader + "[Slice]\nManagedOOMSwap=kill\n", label));
    // session.slice holds the desktop itself; app.slice everything launched.
    steps.append(writeFileStep(kSession + kFile,
                               kHeader + "[Slice]\nMemoryMin=" + floor + "\nCPUWeight=300\nIOWeight=500\n", label));
    steps.append(writeFileStep(kApps + kFile,
                               kHeader + "[Slice]\nCPUWeight=60\nIOWeight=80\nManagedOOMMemoryPressure=kill\n"
                                         "ManagedOOMMemoryPressureLimit=50%\n",
                               label));
    // Act well before swap is full: grinding through it is the freeze.
    steps.append(writeFileStep(kOomd + kFile,
                               kHeader + "[OOM]\nSwapUsedLimit=40%\nDefaultMemoryPressureLimit=50%\n"
                                         "DefaultMemoryPressureDurationSec=20s\n",
                               label));
    steps.append({{QStringLiteral("systemctl"), QStringLiteral("daemon-reload")},
                  true, Step::Tool::Generic, tr("Reloading the service manager")});
    steps.append({{QStringLiteral("systemctl"), QStringLiteral("enable"), QStringLiteral("--now"), kUnit},
                  true, Step::Tool::Generic, tr("Switching the memory watchdog on")});
    return steps;
}

} // namespace gw
