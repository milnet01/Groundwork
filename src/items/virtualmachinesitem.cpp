#include "virtualmachinesitem.h"

#include <QCoreApplication>

namespace gw {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("gw::VirtualMachinesItem", text); }

// True when a flags line lists vmx or svm as a whole word.
bool canVirtualise(const QByteArray &cpuinfo)
{
    for (const QList<QByteArray> lines = cpuinfo.split('\n'); const QByteArray &line : lines) { // cppcheck-suppress useStlAlgorithm
        const qsizetype colon = line.indexOf(':');
        if (colon < 0 || line.left(colon).trimmed() != "flags")
            continue;
        const QList<QByteArray> flags = line.mid(colon + 1).simplified().split(' ');
        if (flags.contains("vmx") || flags.contains("svm"))
            return true;
    }
    return false;
}
} // namespace

QString VirtualMachinesItem::title() const { return tr("Virtual machines"); }

QString VirtualMachinesItem::applySentence() const
{
    return tr("Installs Virtual Machine Manager, which runs other operating systems in a window. "
              "It is ready after a restart.");
}

CheckResult VirtualMachinesItem::check(const CheckContext &context) const
{
    const std::optional<QByteArray> cpuinfo = context.readFile(QStringLiteral("/proc/cpuinfo"));
    if (!cpuinfo)
        return {CheckState::CouldNotTell, tr("The processor's details could not be read.")};
    if (!canVirtualise(*cpuinfo))
        return {CheckState::NotNeeded, tr("This computer's processor cannot run virtual machines.")};
    for (const QString &package : {QStringLiteral("virt-manager"), QStringLiteral("libvirt-daemon-qemu")}) {
        const CommandResult r = context.run({QStringLiteral("rpm"), QStringLiteral("-q"), package});
        if (!r.started || r.timedOut)
            return {CheckState::CouldNotTell, tr("The installed packages could not be listed.")};
        if (r.exitCode != 0)
            return {CheckState::NotDone, tr("Virtual Machine Manager is not installed.")};
    }
    return {CheckState::Done, {}};
}

QList<Step> VirtualMachinesItem::applySteps(const SystemIdentity &, const CheckContext &) const
{
    return {{{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"), QStringLiteral("-t"),
              QStringLiteral("pattern"), QStringLiteral("kvm_tools")},
             true, Step::Tool::Zypper, tr("Installing Virtual Machine Manager")}};
}

} // namespace gw
