#include "hibernateitem.h"

#include <QCoreApplication>
#include <QMap>

#include <optional>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::HibernateItem", text); }

const QString kSecureBootVar =
    QStringLiteral("/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c");
const QString kDropInDir = QStringLiteral("/etc/systemd/sleep.conf.d");
const QString kDropIn = QStringLiteral("/99-groundwork-hibernate.conf");

// What the apply writes, and so what "done" means.
const QMap<QByteArray, QByteArray> kWanted = {
    {"AllowSuspend", "no"},
    {"AllowHibernation", "yes"},
    {"AllowSuspendThenHibernate", "no"},
    {"AllowHybridSleep", "no"},
    {"HibernateMode", "shutdown"},
};

// efivarfs: 4 bytes of attributes, then the value; 1 means on. A computer
// started without UEFI has no such variable, and no Secure Boot.
bool secureBootOn(const CheckContext &context)
{
    const auto data = context.readFile(kSecureBootVar);
    return data && data->size() > 4 && data->at(4) == 1;
}

// The size in KiB of the active swap the kernel wakes from, or nothing.
// /sys/power/resume holds that device as "major:minor"; "0:0" means none.
// Only a whole device listed in /proc/swaps counts, so a swap file, whose
// line names no block device, is never offered.
std::optional<qint64> resumeSwapKiB(const CheckContext &context)
{
    const QByteArray resume = context.readFile(QStringLiteral("/sys/power/resume")).value_or(QByteArray()).trimmed();
    if (resume.isEmpty() || resume == "0:0")
        return std::nullopt;
    const QList<QByteArray> lines = context.readFile(QStringLiteral("/proc/swaps")).value_or(QByteArray()).split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() < 3 || !fields[0].startsWith("/dev/"))
            continue;
        const QString name = QString::fromUtf8(fields[0].mid(int(qstrlen("/dev/"))));
        const QByteArray dev =
            context.readFile(QStringLiteral("/sys/class/block/%1/dev").arg(name)).value_or(QByteArray()).trimmed();
        bool ok = false;
        const qint64 size = fields[2].toLongLong(&ok);
        if (dev == resume && ok)
            return size;
    }
    return std::nullopt;
}

qint64 memoryKiB(const CheckContext &context)
{
    for (const QList<QByteArray> lines = context.readFile(QStringLiteral("/proc/meminfo")).value_or(QByteArray()).split('\n'); const QByteArray &line : lines)
        if (line.startsWith("MemTotal:"))
            return line.mid(int(qstrlen("MemTotal:"))).trimmed().split(' ').value(0).toLongLong();
    return 0;
}

// The sleep settings systemd uses: the first main file found, then the
// drop-ins in file-name order, one in a later folder replacing a drop-in of
// the same name in an earlier one; the last setting wins (man sleep.conf.d).
QMap<QByteArray, QByteArray> sleepSettings(const CheckContext &context)
{
    QStringList files;
    for (const QString &dir : {QStringLiteral("/etc/systemd/"), QStringLiteral("/run/systemd/"),
                               QStringLiteral("/usr/local/lib/systemd/"), QStringLiteral("/usr/lib/systemd/")})
        if (context.fileExists(dir + QStringLiteral("sleep.conf"))) {
            files << dir + QStringLiteral("sleep.conf");
            break;
        }
    QMap<QString, QString> dropIns;
    for (const QString &dir : {QStringLiteral("/usr/lib/systemd/sleep.conf.d/"),
                               QStringLiteral("/usr/local/lib/systemd/sleep.conf.d/"),
                               QStringLiteral("/run/systemd/sleep.conf.d/"), QStringLiteral("/etc/systemd/sleep.conf.d/")})
        for (const QStringList names = context.entries(dir); const QString &name : names)
            if (name.endsWith(QLatin1String(".conf")))
                dropIns.insert(name, dir + name);
    files += dropIns.values();

    QMap<QByteArray, QByteArray> settings;
    for (const QString &file : std::as_const(files))
        for (const QList<QByteArray> lines = context.readFile(file).value_or(QByteArray()).split('\n'); const QByteArray &line : lines) {
            const qsizetype eq = line.indexOf('=');
            const QByteArray key = line.left(eq).trimmed();
            if (eq > 0 && kWanted.contains(key))
                settings.insert(key, line.mid(eq + 1).trimmed());
        }
    return settings;
}

// systemd reads yes, true, on and 1 alike, and no, false, off and 0.
QByteArray normalised(QByteArray value)
{
    value = value.toLower();
    if (value == "true" || value == "on" || value == "1")
        return "yes";
    if (value == "false" || value == "off" || value == "0")
        return "no";
    return value;
}
} // namespace

QString HibernateItem::title() const { return tr("Hibernate instead of sleep"); }

QString HibernateItem::applySentence() const
{
    return tr("Turns sleep off and keeps hibernate, which saves your work to the disk and switches the computer "
              "off. For computers that do not wake properly from sleep.");
}

CheckResult HibernateItem::check(const CheckContext &context) const
{
    if (secureBootOn(context))
        return {CheckState::NotNeeded, tr("Secure Boot is on, and it stops the computer hibernating.")};
    const QList<QByteArray> states =
        context.readFile(QStringLiteral("/sys/power/state")).value_or(QByteArray()).simplified().split(' ');
    if (!states.contains("disk"))
        return {CheckState::NotNeeded, tr("This computer cannot hibernate.")};
    const std::optional<qint64> swap = resumeSwapKiB(context);
    if (!swap)
        return {CheckState::NotNeeded, tr("The computer is not set up to wake from hibernation. This item never "
                                          "changes start-up settings.")};
    const qint64 memory = memoryKiB(context);
    if (memory <= 0)
        return {CheckState::CouldNotTell, tr("How much memory the computer has could not be read.")};
    if (*swap < memory)
        return {CheckState::NotNeeded, tr("The swap space the computer wakes from is smaller than its memory, so it "
                                          "might not hold everything.")};

    const QMap<QByteArray, QByteArray> settings = sleepSettings(context);
    for (auto it = kWanted.cbegin(); it != kWanted.cend(); ++it)
        if (normalised(settings.value(it.key())) != it.value())
            return {CheckState::NotDone, tr("The sleep settings are not yet the ones this item sets.")};
    return {CheckState::Done, {}};
}

QList<Step> HibernateItem::applySteps(const SystemIdentity &, const CheckContext &) const
{
    const QString label = tr("Turning sleep off and keeping hibernate");
    QByteArray text = "# Written by Groundwork: hibernate instead of sleep. Remove this file to undo.\n[Sleep]\n";
    for (auto it = kWanted.cbegin(); it != kWanted.cend(); ++it)
        text += it.key() + '=' + it.value() + '\n';
    return {makeDirectoriesStep({kDropInDir}, label), writeFileStep(kDropInDir + kDropIn, text, label)};
}

} // namespace gw
