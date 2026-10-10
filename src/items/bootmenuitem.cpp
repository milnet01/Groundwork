#include "bootmenuitem.h"

#include <QCoreApplication>

#include <optional>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::BootMenuItem", text); }

constexpr int kWait = 3;
const QString kDefaultGrub = QStringLiteral("/etc/default/grub");
const QString kTimeoutVar =
    QStringLiteral("/sys/firmware/efi/efivars/LoaderConfigTimeout-4a67b082-0a4c-41cf-b6c7-440b29bb8c4f");

enum class Loader { None, Sdbootutil, Grub };

// openSUSE's installer records the boot loader here.
Loader loader(const CheckContext &context)
{
    for (const QByteArray &line : context.readFile(QStringLiteral("/etc/sysconfig/bootloader"))
                                      .value_or(QByteArray()).split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (!trimmed.startsWith("LOADER_TYPE="))
            continue;
        const QByteArray type = trimmed.mid(int(qstrlen("LOADER_TYPE="))).replace('"', "");
        if (type == "systemd-boot" || type == "grub2-bls")
            return Loader::Sdbootutil;
        if (type == "grub2" || type == "grub2-efi")
            return Loader::Grub;
    }
    return Loader::None;
}

// efivarfs: 4 bytes of attributes, then the value in UTF-16LE.
QString efiString(const QByteArray &data)
{
    QString value;
    for (qsizetype i = 4; i + 1 < data.size(); i += 2) {
        const char16_t c = char16_t(uchar(data[i])) | char16_t(uchar(data[i + 1]) << 8);
        if (c == 0)
            break;
        value += QChar(c);
    }
    return value;
}

// Seconds, as sdbootutil and grub write them; a hidden menu waits 0 and a
// forced one (-1) waits until a choice is made.
std::optional<int> seconds(QString value)
{
    value = value.trimmed().remove(QLatin1Char('"'));
    if (value == QLatin1String("menu-hidden") || value == QLatin1String("menu-disabled"))
        return 0;
    if (value == QLatin1String("menu-force") || value == QLatin1String("4294967295"))
        return -1;
    bool ok = false;
    const int n = value.toInt(&ok);
    return ok ? std::optional<int>(n) : std::nullopt;
}

// The GRUB_TIMEOUT line of /etc/default/grub, or empty.
QByteArray grubTimeoutLine(const CheckContext &context)
{
    QByteArray found;
    for (const QByteArray &line : context.readFile(kDefaultGrub).value_or(QByteArray()).split('\n'))
        if (line.trimmed().startsWith("GRUB_TIMEOUT="))
            found = line.trimmed();
    return found;
}

std::optional<int> wait(const CheckContext &context, Loader type)
{
    if (type == Loader::Grub) {
        const QByteArray line = grubTimeoutLine(context);
        return line.isEmpty() ? 5 : seconds(QString::fromUtf8(line.mid(int(qstrlen("GRUB_TIMEOUT=")))));
    }
    if (const auto data = context.readFile(kTimeoutVar))
        return seconds(efiString(*data));
    const CommandResult result = context.run({QStringLiteral("sdbootutil"), QStringLiteral("get-timeout")});
    return result.ok() ? seconds(QString::fromUtf8(result.out)) : std::nullopt;
}
} // namespace

QString BootMenuItem::title() const { return tr("A shorter wait at the boot menu"); }

QString BootMenuItem::applySentence() const
{
    return tr("Shows the boot menu for 3 seconds, so the computer starts sooner. There is still time to "
              "choose from it.");
}

CheckResult BootMenuItem::check(const CheckContext &context) const
{
    const Loader type = loader(context);
    if (type == Loader::None)
        return {CheckState::NotNeeded, tr("No boot menu this can change was found.")};
    const std::optional<int> s = wait(context, type);
    if (!s)
        return {CheckState::CouldNotTell, tr("Only the administrator can read how long the boot menu waits.")};
    if (*s >= 0 && *s <= kWait)
        return {CheckState::Done, {}};
    return {CheckState::NotDone, tr("The boot menu waits longer than 3 seconds.")};
}

QList<Step> BootMenuItem::applySteps(const SystemIdentity &, const CheckContext &context) const
{
    const QString label = tr("Shortening the wait at the boot menu");
    const QString setting = QStringLiteral("GRUB_TIMEOUT=%1").arg(kWait);
    if (loader(context) == Loader::Sdbootutil)
        return {Step{{QStringLiteral("sdbootutil"), QStringLiteral("set-timeout"), QString::number(kWait)},
                     true, Step::Tool::Generic, label}};
    const QStringList edit = grubTimeoutLine(context).isEmpty()
        ? QStringList{QStringLiteral("sed"), QStringLiteral("-i"), QStringLiteral("$a") + setting, kDefaultGrub}
        : QStringList{QStringLiteral("sed"), QStringLiteral("-i"), QStringLiteral("-E"),
                      QStringLiteral("s/^GRUB_TIMEOUT=.*/") + setting + QLatin1Char('/'), kDefaultGrub};
    return {Step{edit, true, Step::Tool::Generic, label},
            Step{{QStringLiteral("grub2-mkconfig"), QStringLiteral("-o"), QStringLiteral("/boot/grub2/grub.cfg")},
                 true, Step::Tool::Generic, label}};
}

} // namespace gw
