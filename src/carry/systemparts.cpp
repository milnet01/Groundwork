// System-wide settings in a saved setup (GRND-0073;
// docs/specs/GRND-0073-system-settings.md): which are offered, and what of a
// saved file may reach a root command. This is a trust boundary: the file may
// have been edited, and restoring writes to /etc as root.
#include "carry.h"

#include "core/folderlist.h"
#include "core/translations.h"
#include "items/networksharesitem.h"
#include "items/service.h"

#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>

namespace gw {
namespace {

constexpr int kLongTimeoutMs = 10 * 60 * 1000; // tar over a large file

bool isAtOrBelow(const QString &path, const QString &dir)
{
    return path == dir || path.startsWith(dir + QLatin1Char('/'));
}

bool isTrue(const QString &value)
{
    const QString lower = value.toLower();
    return lower == QLatin1String("yes") || lower == QLatin1String("true") || lower == QLatin1String("1");
}

bool isFalse(const QString &value)
{
    const QString lower = value.toLower();
    return lower == QLatin1String("no") || lower == QLatin1String("false") || lower == QLatin1String("0");
}

// A section of smb.conf, with only what decides whether it travels.
struct ShareSection
{
    QString name, path;
    bool guestOk = false;
    bool write = false;
};

QList<ShareSection> readShareSections(const QByteArray &smbConf)
{
    QList<ShareSection> sections;
    for (const QString &raw : QString::fromUtf8(smbConf).split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            sections.append({line.mid(1, line.size() - 2).trimmed(), {}});
            continue;
        }
        if (sections.isEmpty())
            continue;
        const QString key = line.section(QLatin1Char('='), 0, 0).trimmed().toLower();
        const QString value = line.section(QLatin1Char('='), 1).trimmed();
        ShareSection &section = sections.last();
        if (key == QLatin1String("path"))
            section.path = value;
        else if (key == QLatin1String("guest ok"))
            section.guestOk = isTrue(value);
        else if (key == QLatin1String("read only"))
            section.write = section.write || isFalse(value);
        else if (key == QLatin1String("writable") || key == QLatin1String("writeable"))
            section.write = section.write || isTrue(value);
    }
    return sections;
}

} // namespace

std::optional<SystemPart> systemPartNamed(const QString &kind)
{
    if (kind == QLatin1String("firewall"))
        return SystemPart{kind, kFirewallPart, QStringLiteral("Firewall"), QStringLiteral("/etc/firewalld"), false};
    if (kind == QLatin1String("printers"))
        return SystemPart{kind, kPrintersPart, QStringLiteral("Printers"), QStringLiteral("/etc/cups"), true};
    return std::nullopt;
}

bool isSystemMember(const QString &name, const QString &partPath)
{
    static const QRegularExpression firewall(
        QStringLiteral(R"(\A\.groundwork-system/firewall/(?:firewalld\.conf|(?:zones|services|policies|ipsets)/[A-Za-z0-9_.+-]+\.xml)\z)"));
    static const QRegularExpression printers(
        QStringLiteral(R"(\A\.groundwork-system/printers/(?:printers\.conf|classes\.conf|ppd/[A-Za-z0-9_.+-]+\.ppd)\z)"));
    if (partPath == kFirewallPart)
        return firewall.match(name).hasMatch();
    return partPath == kPrintersPart && printers.match(name).hasMatch();
}

QStringList findSystemFilesArguments(const SystemPart &part)
{
    return {QStringLiteral("find"), part.source, QStringLiteral("-type"), QStringLiteral("f"), QStringLiteral("-printf"),
            QStringLiteral("%P\n")};
}

QStringList saveableSystemFiles(const SystemPart &part, const QStringList &found)
{
    QStringList files;
    for (const QString &file : found) {
        if (isSystemMember(part.path + QLatin1Char('/') + file, part.path))
            files << file;
    }
    return files;
}

QStringList createSystemArguments(const SystemPart &part, const QString &archive)
{
    return {QStringLiteral("tar"), QStringLiteral("--create"), QStringLiteral("--format=posix"),
            QStringLiteral("--ignore-failed-read"), QStringLiteral("--file"), archive, QStringLiteral("-C"), part.source,
            QStringLiteral("--transform=s,^,") + part.path + QStringLiteral("/,"), QStringLiteral("--verbatim-files-from"),
            QStringLiteral("--null"), QStringLiteral("--files-from=-")};
}

SystemMembers systemMembers(const CheckContext &context, const QString &file, const QString &partPath)
{
    SystemMembers result;
    const QStringList tar = QStringList{QStringLiteral("tar")} + listArguments(file);
    const CommandResult names = context.run(tar, kLongTimeoutMs, kMemberListCap);
    const CommandResult verbose = context.run(tar + QStringList{QStringLiteral("--verbose")}, kLongTimeoutMs, kMemberListCap);
    const QStringList members = QString::fromUtf8(names.out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    const QStringList kinds = QString::fromUtf8(verbose.out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    // The two lines of one member say its name and its type: anything else
    // and neither can be trusted.
    if (!names.ok() || !verbose.ok() || members.size() != kinds.size()) {
        result.problem = QCoreApplication::translate("gw::Carry", "The saved file could not be read.");
        return result;
    }
    QStringList files;
    const QString under = partPath + QLatin1Char('/');
    for (qsizetype i = 0; i < members.size(); ++i) {
        const QString &member = members[i];
        if (member != partPath && !member.startsWith(under))
            continue;
        QString folder = member;
        while (folder.endsWith(QLatin1Char('/')))
            folder.chop(1);
        const bool partFolder = folder == partPath
            || (partPath == kFirewallPart
                && (folder == under + QLatin1String("zones") || folder == under + QLatin1String("services")
                    || folder == under + QLatin1String("policies") || folder == under + QLatin1String("ipsets")))
            || (partPath == kPrintersPart && folder == under + QLatin1String("ppd"));
        if (kinds[i].startsWith(QLatin1Char('d')) && partFolder)
            continue; // made from a fixed list, never extracted
        if (!kinds[i].startsWith(QLatin1Char('-')) || !isSystemMember(member, partPath)) {
            result.problem = QCoreApplication::translate("gw::Carry", "The saved file holds %1, which Groundwork will not "
                                                                      "restore, so this part was left alone.")
                                 .arg(shownFromFile(member));
            return result;
        }
        files << member;
    }
    result.files = files;
    return result;
}

bool isCarriedMount(const CarriedMount &mount)
{
    static const QRegularExpression source(
        QStringLiteral(R"(\A(?:UUID=|LABEL=|/dev/disk/by-uuid/|/dev/disk/by-label/)(?!\.{1,2}\z)[^\x00-\x20\x7f/\\]+\z)"));
    static const QRegularExpression point(QStringLiteral(R"(\A/[^\x00-\x20\x7f\\]+\z)"));
    static const QRegularExpression fstype(QStringLiteral(R"(\A[A-Za-z0-9_.+-]+\z)"));
    static const QRegularExpression options(QStringLiteral(R"(\A[A-Za-z0-9_,=.:+-]*\z)"));
    if (!source.match(mount.source).hasMatch() || !point.match(mount.point).hasMatch()
        || mount.point != QDir::cleanPath(mount.point) || !fstype.match(mount.fstype).hasMatch()
        || !options.match(mount.options).hasMatch())
        return false;
    for (const char *dir : {"/bin", "/boot", "/dev", "/etc", "/home", "/lib", "/lib64", "/opt", "/proc", "/root", "/run",
                            "/sbin", "/srv", "/sys", "/tmp", "/usr", "/var"}) {
        if (isAtOrBelow(mount.point, QLatin1String(dir)))
            return false;
    }
    for (const char *type : {"swap", "nfs", "nfs4", "cifs", "smb3", "sshfs", "fuse.sshfs"}) {
        if (mount.fstype == QLatin1String(type))
            return false;
    }
    const QString lower = mount.options.toLower();
    if (mount.fstype == QLatin1String("btrfs") && lower.contains(QLatin1String("subvol")))
        return false;
    return !lower.contains(QLatin1String("password")) && !lower.contains(QLatin1String("credentials"))
           && !lower.contains(QLatin1String("user="));
}

QList<CarriedMount> carriedMounts(const QByteArray &fstabText)
{
    QList<CarriedMount> mounts;
    for (const QString &line : QString::fromUtf8(fstabText).split(QLatin1Char('\n'))) {
        const QStringList fields = line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() < 4 || fields[0].startsWith(QLatin1Char('#')))
            continue;
        const CarriedMount mount{fields[0], fields[1], fields[2], fields[3]};
        if (isCarriedMount(mount))
            mounts << mount;
    }
    return mounts;
}

FoundShares carriedShares(const QByteArray &smbConf, const QString &currentValue)
{
    FoundShares found;
    const NetworkSharesItem rules;
    auto add = [&](const QString &path, bool write) {
        if (std::none_of(found.shares.cbegin(), found.shares.cend(), [&](const CarriedShare &s) { return s.path == path; }))
            found.shares.append({path, write});
    };
    for (const QList<ListedFolder> folders = parseFolderList(currentValue).value_or(QList<ListedFolder>{});
         const ListedFolder &folder : folders) {
        if (rules.whyRefused(folder.path).isEmpty())
            add(folder.path, folder.write);
    }
    for (const QList<ShareSection> sections = readShareSections(smbConf); const ShareSection &section : sections) {
        const QString lower = section.name.toLower();
        if (lower == QLatin1String("global") || lower == QLatin1String("homes") || lower == QLatin1String("printers")
            || lower == QLatin1String("print$") || section.path.isEmpty())
            continue;
        if (!section.guestOk) {
            found.notCarried << QCoreApplication::translate("gw::Carry", "%1 is not carried: it asks for a password.")
                                    .arg(shownFromFile(section.name));
        } else if (const QString why = rules.whyRefused(section.path); !why.isEmpty()) {
            found.notCarried << QCoreApplication::translate("gw::Carry", "%1 is not carried: %2")
                                    .arg(shownFromFile(section.name), why);
        } else {
            add(section.path, section.write);
        }
    }
    return found;
}

void gatherSystem(const CheckContext &context, Gathered *into)
{
    into->firewallFound = readService(context, QStringLiteral("firewalld")).found;
    if (const CommandResult lpstat = context.run({QStringLiteral("lpstat"), QStringLiteral("-e")}); lpstat.ok())
        into->printers = QString::fromUtf8(lpstat.out).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (const std::optional<QByteArray> fstab = context.readFile(QStringLiteral("/etc/fstab")))
        into->mounts = carriedMounts(*fstab);
    const FoundShares shares = carriedShares(context.readFile(QStringLiteral("/etc/samba/smb.conf")).value_or(QByteArray()),
                                             NetworkSharesItem().check(context).currentValue);
    into->shares = shares.shares;
    into->sharesNotCarried = shares.notCarried;
}

} // namespace gw
