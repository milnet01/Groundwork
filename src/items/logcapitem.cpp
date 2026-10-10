#include "logcapitem.h"

#include <QCoreApplication>
#include <QMap>
#include <QRegularExpression>

#include <optional>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::LogCapItem", text); }

const QString kDropInDir = QStringLiteral("/etc/systemd/journald.conf.d");
const QString kDropIn = QStringLiteral("99-groundwork-log-cap.conf");
constexpr double kCap = 1024.0 * 1024 * 1024;

// Lowest priority first (journald.conf(5), Configuration directories).
const QStringList kDirs = {
    QStringLiteral("/usr/lib/systemd/"), QStringLiteral("/usr/local/lib/systemd/"),
    QStringLiteral("/run/systemd/"),     QStringLiteral("/etc/systemd/"),
};

// systemd reads the first journald.conf found from the highest-priority
// directory, then the drop-ins in file-name order, where one in a higher
// directory replaces a drop-in of the same name. The last setting wins.
QByteArray systemMaxUse(const CheckContext &context)
{
    QStringList files;
    for (auto dir = kDirs.crbegin(); dir != kDirs.crend(); ++dir)
        if (context.fileExists(*dir + QStringLiteral("journald.conf"))) {
            files << *dir + QStringLiteral("journald.conf");
            break;
        }
    QMap<QString, QString> dropIns;
    for (const QString &dir : kDirs)
        for (const QString &name : context.entries(dir + QStringLiteral("journald.conf.d/")))
            if (name.endsWith(QLatin1String(".conf")))
                dropIns.insert(name, dir + QStringLiteral("journald.conf.d/") + name);
    files += dropIns.values();

    QByteArray value;
    for (const QString &file : files)
        for (const QByteArray &line : context.readFile(file).value_or(QByteArray()).split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.startsWith("SystemMaxUse="))
                value = trimmed.mid(int(qstrlen("SystemMaxUse="))).trimmed();
        }
    return value;
}

// A size as systemd writes one: a number and a power-of-1024 unit.
std::optional<double> bytes(const QByteArray &value)
{
    static const QRegularExpression size(QStringLiteral("^(\\d+(?:\\.\\d+)?)\\s*([KMGTPE]?)B?$"));
    const QRegularExpressionMatch m = size.match(QString::fromLatin1(value));
    if (!m.hasMatch())
        return std::nullopt;
    double result = m.captured(1).toDouble();
    const qsizetype power = m.captured(2).isEmpty() ? 0 : QStringLiteral("KMGTPE").indexOf(m.captured(2)) + 1;
    for (qsizetype i = 0; i < power; ++i)
        result *= 1024;
    return result;
}
} // namespace

QString LogCapItem::title() const { return tr("A size limit on the system log"); }

QString LogCapItem::applySentence() const
{
    return tr("Keeps the system's log to at most 1 GB, deleting the oldest entries first, so it can never fill "
              "the drive.");
}

CheckResult LogCapItem::check(const CheckContext &context) const
{
    const std::optional<double> cap = bytes(systemMaxUse(context));
    if (cap && *cap <= kCap)
        return {CheckState::Done, {}};
    return {CheckState::NotDone, tr("The system's log may grow larger than 1 GB.")};
}

QList<Step> LogCapItem::applySteps(const SystemIdentity &, const CheckContext &) const
{
    const QString label = tr("Limiting the size of the system log");
    return {makeDirectoriesStep({kDropInDir}, label),
            writeFileStep(kDropInDir + QLatin1Char('/') + kDropIn,
                          "# Written by Groundwork: a size limit on the system log. Remove this file to undo.\n"
                          "[Journal]\nSystemMaxUse=1G\nSystemKeepFree=2G\nSystemMaxFileSize=128M\n"
                          "MaxRetentionSec=3month\n",
                          label),
            Step{{QStringLiteral("systemctl"), QStringLiteral("restart"), QStringLiteral("systemd-journald")},
                 true, Step::Tool::Generic, label}};
}

} // namespace gw
