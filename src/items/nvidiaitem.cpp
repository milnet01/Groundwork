#include "nvidiaitem.h"

#include "hardware.h"
#include "packman.h"

#include <QCoreApplication>

namespace gw {
namespace {

QString tr(const char *text) { return QCoreApplication::translate("gw::NvidiaItem", text); }

const QString kRepoAlias = QStringLiteral("NVIDIA");

QStringList packagesFor(NvidiaItem::Driver driver)
{
    if (driver == NvidiaItem::Driver::OpenG07)
        return {QStringLiteral("nvidia-open-driver-G07-signed-kmp-meta"), QStringLiteral("nvidia-userspace-meta-G07")};
    if (driver == NvidiaItem::Driver::ProprietaryG06)
        return {QStringLiteral("nvidia-driver-G06-kmp-meta"), QStringLiteral("nvidia-userspace-meta-G06")};
    return {};
}

} // namespace

QString NvidiaItem::title() const { return tr("NVIDIA graphics driver"); }

QString NvidiaItem::applySentence() const
{
    return tr("Installs NVIDIA's driver for this graphics card, which accepts NVIDIA's licence. "
              "It takes effect after a restart.");
}

NvidiaItem::Driver NvidiaItem::driverFor(const CheckContext &context)
{
    Driver best = Driver::None;
    for (const PciDevice &d : pciDevices(context)) {
        if (d.vendor != QLatin1String("0x10de")
            || !(d.cls.startsWith(QLatin1String("0x0300")) || d.cls.startsWith(QLatin1String("0x0302"))))
            continue;
        bool ok = false;
        const uint id = d.device.toUInt(&ok, 16);
        if (!ok)
            continue;
        const Driver driver = id >= 0x1E02 ? Driver::OpenG07 : id >= 0x1340 ? Driver::ProprietaryG06 : Driver::TooOld;
        // With several cards, the newest decides.
        if (best == Driver::None || driver == Driver::OpenG07 || (driver == Driver::ProprietaryG06 && best == Driver::TooOld))
            best = driver;
    }
    return best;
}

CheckResult NvidiaItem::check(const CheckContext &context) const
{
    const Driver driver = driverFor(context);
    if (driver == Driver::None)
        return {CheckState::NotNeeded, tr("No NVIDIA graphics card was found.")};
    if (driver == Driver::TooOld)
        return {CheckState::NotNeeded, tr("This NVIDIA card is too old for NVIDIA's current drivers; "
                                          "the built-in driver is used.")};
    if (context.fileExists(QStringLiteral("/sys/module/nvidia")))
        return {CheckState::Done, {}};
    QStringList query{QStringLiteral("rpm"), QStringLiteral("-q")};
    query << packagesFor(driver).first();
    const CommandResult installed = context.run(query);
    if (!installed.started || installed.timedOut)
        return {CheckState::CouldNotTell, tr("The installed packages could not be listed.")};
    if (installed.exitCode == 0)
        return {CheckState::Done, tr("Installed; it takes effect after a restart.")};
    QString detail = tr("This computer has an NVIDIA card without NVIDIA's driver.");
    if (secureBootOn(context))
        detail += QLatin1Char(' ') + tr("Secure Boot is on, so you may be asked to approve a key "
                                         "at the next restart.");
    return {CheckState::NotDone, detail};
}

QList<Step> NvidiaItem::applySteps(const SystemIdentity &system, const CheckContext &context) const
{
    QList<Step> steps;
    QString alias = repositoryAlias(context, QStringLiteral("download.nvidia.com"));
    if (alias.isEmpty()) {
        alias = kRepoAlias;
        // Slowroll uses the Tumbleweed repository; Leap's path takes its
        // version, which zypper expands from $releasever.
        const QString url = system.family == SystemIdentity::Family::Leap
            ? QStringLiteral("https://download.nvidia.com/opensuse/leap/$releasever")
            : QStringLiteral("https://download.nvidia.com/opensuse/tumbleweed");
        steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("addrepo"),
                       QStringLiteral("--refresh"), url, alias},
                      true, Step::Tool::Zypper, tr("Adding NVIDIA's software source")});
    }
    steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("--gpg-auto-import-keys"),
                   QStringLiteral("refresh"), alias},
                  true, Step::Tool::Zypper, tr("Reading NVIDIA's software list")});
    QStringList install{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"),
                        QStringLiteral("--auto-agree-with-licenses")};
    install << packagesFor(driverFor(context));
    steps.append({install, true, Step::Tool::Zypper, tr("Installing the NVIDIA driver")});
    return steps;
}

} // namespace gw
