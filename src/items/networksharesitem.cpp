#include "networksharesitem.h"

#include "core/folderlist.h"
#include "core/translations.h"
#include "service.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <pwd.h>
#include <unistd.h>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::NetworkSharesItem", text); }

const QString kConf = QStringLiteral("/etc/samba/smb.conf");
const QString kShares = QStringLiteral("/etc/samba/groundwork-shares.conf");
const QByteArray kIncludeLine = "include = /etc/samba/groundwork-shares.conf";
const QStringList kUnits = {QStringLiteral("smb"), QStringLiteral("nmb"), QStringLiteral("wsdd")};
constexpr int kMaxPathBytes = 1024;
constexpr int kMaxNameLength = 64;

// Samba's own sections, and those a package's smb.conf brings. They are fixed
// here because before Samba is installed there is no smb.conf to read them from.
const QStringList kReservedNames = {QStringLiteral("global"),  QStringLiteral("homes"),   QStringLiteral("printers"),
                                    QStringLiteral("print$"),  QStringLiteral("ipc$"),    QStringLiteral("profiles"),
                                    QStringLiteral("users"),   QStringLiteral("groups"),  QStringLiteral("netlogon")};

bool isAtOrBelow(const QString &path, const QString &dir)
{
    return path == dir || path.startsWith(dir + QLatin1Char('/'));
}

struct Section
{
    QString name, path;
    bool readOnly = true;
};

// A Samba file's sections, with the two settings this item reads. A comment
// line is skipped; a section the file repeats is listed twice.
QList<Section> readSections(const QByteArray &text)
{
    QList<Section> sections;
    for (const QString &raw : QString::fromUtf8(text).split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            sections.append({line.mid(1, line.size() - 2).trimmed(), {}});
        } else if (!sections.isEmpty() && !line.startsWith(QLatin1Char('#')) && !line.startsWith(QLatin1Char(';'))) {
            const QString key = line.section(QLatin1Char('='), 0, 0).trimmed().toLower();
            const QString setting = line.section(QLatin1Char('='), 1).trimmed();
            if (key == QLatin1String("path"))
                sections.last().path = setting;
            else if (key == QLatin1String("read only"))
                sections.last().readOnly = setting.toLower() != QLatin1String("no");
        }
    }
    return sections;
}

bool isReserved(const QString &name)
{
    return kReservedNames.contains(name, Qt::CaseInsensitive);
}

bool holdsIncludeLine(const QByteArray &conf)
{
    for (const QByteArray &line : conf.split('\n'))
        if (line.trimmed() == kIncludeLine)
            return true;
    return false;
}

// The packages this system lacks.
QStringList packagesToInstall(const CheckContext &context)
{
    QStringList install;
    if (!readService(context, QStringLiteral("smb")).found)
        install << QStringLiteral("samba");
    if (!readService(context, QStringLiteral("wsdd")).found)
        install << QStringLiteral("wsdd");
    return install;
}

// The folders the shares file holds now, as a value.
QString appliedValue(const QByteArray &sharesFile)
{
    QList<ListedFolder> folders;
    for (const Section &section : readSections(sharesFile))
        if (!section.path.isEmpty())
            folders.append({section.path, !section.readOnly});
    return folderListValue(folders);
}

QByteArray sharesFileText(const QList<ListedFolder> &folders, const QStringList &names)
{
    const passwd *me = ::getpwuid(::getuid());
    const QByteArray user = me ? QByteArray(me->pw_name) : QByteArray();
    QByteArray text = "# Written by Groundwork. Change it from Groundwork, not here.\n"
                      "[global]\n"
                      "   map to guest = bad user\n";
    for (qsizetype i = 0; i < folders.size(); ++i) {
        text += "\n[" + names[i].toUtf8() + "]\n"
                "   path = " + folders[i].path.toUtf8() + "\n"
                "   browseable = yes\n"
                "   guest ok = yes\n"
                "   read only = " + (folders[i].write ? "no" : "yes") + "\n"
                "   force user = " + user + "\n"
                "   create mask = 0664\n"
                "   directory mask = 0775\n"
                "   mangled names = no\n";
    }
    return text;
}
} // namespace

QString NetworkSharesItem::whyPathRefused(const QString &path)
{
    if (!QDir::isAbsolutePath(path) || path != QDir::cleanPath(path))
        return tr("This folder's path could not be used.");
    for (const QChar c : path)
        if (c.category() == QChar::Other_Control || c == QLatin1Char('%') || c == QLatin1Char('"')
            || c == QLatin1Char('\\'))
            return tr("A folder whose path holds a control character, %, a double quote or a backslash cannot be shared.");
    // Samba trims the ends of a setting and squeezes spaces inside it (testparm,
    // 2026-10-30), so it would share another folder.
    if (path.startsWith(QLatin1Char(' ')) || path.endsWith(QLatin1Char(' ')) || path.contains(QLatin1String("  ")))
        return tr("A folder whose path starts or ends with a space, or has two spaces together, cannot be shared.");
    if (path.toUtf8().size() > kMaxPathBytes)
        return tr("This folder's path is too long to share.");
    if (path == QLatin1String("/"))
        return tr("The whole computer cannot be shared.");
    for (const char *dir : {"/bin", "/boot", "/dev", "/etc", "/lib", "/lib64", "/opt", "/proc", "/root", "/sbin",
                            "/sys", "/usr", "/var"})
        if (isAtOrBelow(path, QLatin1String(dir)))
            return tr("This folder holds system files, so it cannot be shared.");
    if (isAtOrBelow(path, QStringLiteral("/run")) && !isAtOrBelow(path, QStringLiteral("/run/media")))
        return tr("This folder holds system files, so it cannot be shared.");

    const QString home = QDir::homePath();
    if (path == QLatin1String("/home") || isAtOrBelow(home, path))
        return tr("Your home folder holds keys and passwords, so it cannot be shared whole. Choose a folder inside it.");
    if (isAtOrBelow(path, home)) {
        const QStringList parts = path.mid(home.size()).split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (const QString &part : parts)
            if (part.startsWith(QLatin1Char('.')))
                return tr("Hidden folders in your home folder hold settings and keys, so they cannot be shared.");
    }
    return {};
}

QString NetworkSharesItem::title() const { return tr("Share folders on the network"); }

QString NetworkSharesItem::applySentence() const
{
    return tr("Lets other computers open the folders you list here. Anyone on the network can get in with no "
              "password, on any network this computer joins. Windows 11 may refuse a folder with no password until "
              "a setting is changed on that computer.");
}

QString NetworkSharesItem::valuePrompt() const { return tr("Folders to share:"); }

QString NetworkSharesItem::whyRefused(const QString &path) const
{
    const QString why = whyPathRefused(path);
    if (!why.isEmpty())
        return why;
    // A link leads where it points: Samba follows a share's own path, so the
    // rules hold for that too.
    const QString real = QFileInfo(path).canonicalFilePath();
    return real.isEmpty() || real == path ? QString() : whyPathRefused(real);
}

bool NetworkSharesItem::isValidValue(const QString &value) const
{
    const auto folders = parseFolderList(value);
    if (!folders || folders->size() > kMaxListedFolders)
        return false;
    QSet<QString> seen;
    for (const ListedFolder &folder : *folders) {
        if (seen.contains(folder.path) || !whyRefused(folder.path).isEmpty())
            return false;
        seen.insert(folder.path);
    }
    return true;
}

QStringList NetworkSharesItem::shareNames(const QStringList &paths, const QStringList &taken)
{
    QSet<QString> used;
    for (const QStringList &names : {kReservedNames, taken})
        for (const QString &name : names)
            used.insert(name.toCaseFolded());
    auto trimmed = [](QString name) {
        while (name.startsWith(QLatin1Char(' ')) || name.startsWith(QLatin1Char('.')))
            name.remove(0, 1);
        while (name.endsWith(QLatin1Char(' ')) || name.endsWith(QLatin1Char('.')))
            name.chop(1);
        return name;
    };
    QStringList result;
    for (const QString &path : paths) {
        QString name = path.section(QLatin1Char('/'), -1);
        for (QChar &c : name)
            if (c.category() == QChar::Other_Control || QStringLiteral("[]\"/\\:|<>+=;,?*%").contains(c))
                c = QLatin1Char('_');
        name = trimmed(name).left(kMaxNameLength);
        if (!name.isEmpty() && name.back().isHighSurrogate())
            name.chop(1);
        name = trimmed(name);
        if (name.isEmpty())
            name = QStringLiteral("Folder");
        QString unique = name;
        for (int n = 2; used.contains(unique.toCaseFolded()); ++n)
            unique = QStringLiteral("%1 %2").arg(name).arg(n);
        used.insert(unique.toCaseFolded());
        result << unique;
    }
    return result;
}

CheckResult NetworkSharesItem::check(const CheckContext &context) const
{
    bool running = true;
    for (const QString &unit : kUnits) {
        const ServiceState s = readService(context, unit);
        if (!s.known)
            return {CheckState::CouldNotTell, tr("The service manager did not answer.")};
        running = running && s.found && s.enabled && s.active;
    }
    const auto conf = context.readFile(kConf);
    const auto shares = context.readFile(kShares);

    CheckResult result{running && conf && shares && holdsIncludeLine(*conf) ? CheckState::Done : CheckState::NotDone, {}};
    result.nothingToInstall = packagesToInstall(context).isEmpty();
    if (shares)
        result.currentValue = appliedValue(*shares);
    if (conf) {
        QStringList others;
        for (const Section &section : readSections(*conf)) {
            if (isReserved(section.name))
                continue;
            others << isolated(section.name);
            if (!section.path.isEmpty())
                result.heldFolders.insert(section.path, section.name);
        }
        if (!others.isEmpty())
            result.detail = tr("Other shares, left as they are: %1.").arg(others.join(QStringLiteral(", ")));
    }
    return result;
}

QList<Step> NetworkSharesItem::applySteps(const SystemIdentity &system, const CheckContext &context) const
{
    return applyStepsFor(system, context, appliedValue(context.readFile(kShares).value_or(QByteArray())));
}

QList<Step> NetworkSharesItem::applyStepsFor(const SystemIdentity &, const CheckContext &context,
                                             const QString &value) const
{
    const auto folders = parseFolderList(value);
    if (!folders)
        return {};
    const QByteArray conf = context.readFile(kConf).value_or(QByteArray());
    QStringList paths, taken;
    for (const ListedFolder &folder : *folders)
        paths << folder.path;
    for (const Section &section : readSections(conf))
        taken << section.name;

    QList<Step> steps;
    if (const QStringList install = packagesToInstall(context); !install.isEmpty())
        steps.append({QStringList{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install")} + install,
                      true, Step::Tool::Zypper, tr("Installing file sharing")});

    const QString label = tr("Sharing your folders");
    steps.append(writeFileStep(kShares, sharesFileText(*folders, shareNames(paths, taken)), label));
    // The owner's own file is only ever added to, and only once (spec 4.1).
    if (!holdsIncludeLine(conf))
        steps.append({{QStringLiteral("dd"), QStringLiteral("of=") + kConf, QStringLiteral("oflag=append"),
                       QStringLiteral("conv=notrunc"), QStringLiteral("status=none")},
                      true, Step::Tool::Generic, label, {},
                      "\n# Added by Groundwork: the folders it shares.\n" + kIncludeLine + '\n'});
    // Samba's start-up script labels new folders for SELinux and AppArmor,
    // and rebuilds AppArmor's list only when smb.conf is newer than it; a
    // reload would skip the script altogether (spec 4.6).
    steps.append({{QStringLiteral("touch"), kConf}, true, Step::Tool::Generic, label});
    steps.append({QStringList{QStringLiteral("systemctl"), QStringLiteral("enable")} + kUnits, true,
                  Step::Tool::Generic, label});
    steps.append({QStringList{QStringLiteral("systemctl"), QStringLiteral("restart")} + kUnits, true,
                  Step::Tool::Generic, label});
    if (readService(context, QStringLiteral("firewalld")).active) {
        const QString firewall = tr("Letting file sharing through the firewall");
        for (const char *service : {"samba", "wsdd"})
            steps.append({{QStringLiteral("firewall-cmd"), QStringLiteral("--permanent"),
                           QStringLiteral("--add-service=") + QLatin1String(service)},
                          true, Step::Tool::Generic, firewall});
        steps.append({{QStringLiteral("firewall-cmd"), QStringLiteral("--reload")}, true, Step::Tool::Generic,
                      tr("Reloading the firewall")});
    }
    return steps;
}

} // namespace gw
