#include "sysrqitem.h"

#include <QCoreApplication>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::SysrqItem", text); }

const QString kDir = QStringLiteral("/etc/sysctl.d");
// Sorts after the distribution's 50-default.conf, which sets 184.
const QString kFile = kDir + QStringLiteral("/99-groundwork-sysrq.conf");

// The kernel's bits for the escape's keys: keyboard (R) 4, sync (S) 16,
// read-only remount (U) 32, signals (E, I, F) 64, reboot (B) 128
// (the kernel's sysrq documentation, repeated in 50-default.conf).
constexpr int kEscapeKeys = 4 | 16 | 32 | 64 | 128;
} // namespace

QString SysrqItem::title() const { return tr("Emergency keyboard escape"); }

QString SysrqItem::applySentence() const
{
    return tr("Lets you restart a frozen computer safely from the keyboard, instead of holding the "
              "power button.");
}

CheckResult SysrqItem::check(const CheckContext &context) const
{
    const auto text = context.readFile(QStringLiteral("/proc/sys/kernel/sysrq"));
    bool ok = false;
    const int value = text ? QString::fromLatin1(*text).trimmed().toInt(&ok) : 0;
    if (!ok)
        return {CheckState::CouldNotTell, tr("The kernel's setting could not be read.")};
    if (value == 1 || (value & kEscapeKeys) == kEscapeKeys)
        return {CheckState::Done, {}};
    if (value == 0)
        return {CheckState::NotDone, tr("The escape keys are switched off.")};
    return {CheckState::NotDone, tr("Only some of the escape keys work.")};
}

QList<Step> SysrqItem::applySteps(const SystemIdentity &, const CheckContext &) const
{
    const QString label = tr("Switching the emergency keys on");
    return {makeDirectoriesStep({kDir}, label),
            writeFileStep(kFile,
                          "# Written by Groundwork: emergency keyboard escape. Remove this file to undo.\n"
                          "kernel.sysrq = 1\n",
                          label),
            {{QStringLiteral("sysctl"), QStringLiteral("-p"), kFile}, true, Step::Tool::Generic, label}};
}

} // namespace gw
