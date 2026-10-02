#include "clockitem.h"

#include <QCoreApplication>

namespace gw {
namespace {

QString tr(const char *text) { return QCoreApplication::translate("gw::ClockItem", text); }

const QString kEfiVars = QStringLiteral("/sys/firmware/efi/efivars");
const QString kGlobalGuid = QStringLiteral("-8be4df61-93ca-11d2-aa0d-00e098032b8c");

// An efivarfs Boot#### file: 4 bytes of variable attributes, then an
// EFI_LOAD_OPTION (4 bytes of attributes, 2 bytes of path-list length) and
// its description, NUL-terminated UTF-16LE.
QString bootEntryName(const QByteArray &data)
{
    QString name;
    for (qsizetype i = 10; i + 1 < data.size(); i += 2) {
        const char16_t c = char16_t(uchar(data[i])) | char16_t(uchar(data[i + 1]) << 8);
        if (c == 0)
            break;
        name += QChar(c);
    }
    return name;
}

} // namespace

QString ClockItem::title() const { return tr("Clock shared with Windows"); }

QString ClockItem::applySentence() const
{
    return tr("Keeps the computer's hardware clock in local time, as Windows does, so the "
              "time is right after switching between the two.");
}

bool ClockItem::windowsFound(const CheckContext &context)
{
    if (context.fileExists(kEfiVars)) {
        for (const QString &entry : context.entries(kEfiVars)) {
            if (!entry.startsWith(QLatin1String("Boot")) || !entry.endsWith(kGlobalGuid) || entry.size() != 8 + kGlobalGuid.size())
                continue;
            const auto data = context.readFile(kEfiVars + QLatin1Char('/') + entry);
            if (data && bootEntryName(*data).contains(QLatin1String("Windows"), Qt::CaseInsensitive))
                return true;
        }
        return false;
    }
    // Without UEFI there are no boot entries to read; an NTFS partition is
    // the sign of Windows.
    const CommandResult r = context.run({QStringLiteral("lsblk"), QStringLiteral("-rno"), QStringLiteral("FSTYPE")});
    return r.ok() && r.out.split('\n').contains("ntfs");
}

CheckResult ClockItem::check(const CheckContext &context) const
{
    if (!windowsFound(context))
        return {CheckState::NotNeeded, tr("Windows was not found on this computer.")};
    const CommandResult r = context.run({QStringLiteral("timedatectl"), QStringLiteral("show"),
                                         QStringLiteral("-p"), QStringLiteral("LocalRTC")});
    if (!r.ok())
        return {CheckState::CouldNotTell, tr("The clock settings could not be read.")};
    if (r.out.trimmed() == "LocalRTC=yes")
        return {CheckState::Done, {}};
    return {CheckState::NotDone, tr("Windows is installed, and the clock will be wrong after switching.")};
}

QList<Step> ClockItem::applySteps(const SystemIdentity &, const CheckContext &) const
{
    return {{{QStringLiteral("timedatectl"), QStringLiteral("set-local-rtc"), QStringLiteral("1"),
              QStringLiteral("--adjust-system-clock")},
             true, Step::Tool::Generic, tr("Setting the clock to local time")}};
}

} // namespace gw
