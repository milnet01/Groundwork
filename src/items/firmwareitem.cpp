#include "firmwareitem.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace gw {
namespace {

QString tr(const char *text) { return QCoreApplication::translate("gw::FirmwareItem", text); }

const QStringList kGetUpdates{QStringLiteral("fwupdmgr"), QStringLiteral("get-updates"),
                              QStringLiteral("--json"), QStringLiteral("--no-unreported-check")};

} // namespace

QString FirmwareItem::title() const { return tr("Firmware updates"); }

QString FirmwareItem::applySentence() const
{
    return tr("Installs firmware updates for this computer's hardware from the Linux Vendor "
              "Firmware Service. Some finish at the next restart.");
}

CheckResult FirmwareItem::check(const CheckContext &context) const
{
    const CommandResult r = context.run(kGetUpdates, 90000);
    if (!r.started)
        return {CheckState::NotDone, tr("The firmware updater is not installed.")};
    if (r.exitCode == 2)
        return {CheckState::Done, {}}; // nothing to do
    if (!r.ok())
        return {CheckState::CouldNotTell, tr("The firmware updater could not check for updates.")};
    const QJsonArray devices = QJsonDocument::fromJson(r.out).object().value(QStringLiteral("Devices")).toArray();
    int waiting = 0;
    for (const QJsonValue &device : devices)
        if (!device.toObject().value(QStringLiteral("Releases")).toArray().isEmpty())
            ++waiting;
    if (waiting == 0)
        return {CheckState::Done, {}};
    // Named at the call, so lupdate sees the count and gives it plural forms.
    return {CheckState::NotDone, QCoreApplication::translate("gw::FirmwareItem",
                                                             "%n device(s) have firmware updates waiting.",
                                                             nullptr, waiting)};
}

QList<Step> FirmwareItem::applySteps(const SystemIdentity &, const CheckContext &context) const
{
    QList<Step> steps;
    if (!context.run({QStringLiteral("fwupdmgr"), QStringLiteral("--version")}).started)
        steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"), QStringLiteral("fwupd")},
                      true, Step::Tool::Zypper, tr("Installing the firmware updater")});
    steps.append({{QStringLiteral("fwupdmgr"), QStringLiteral("refresh"), QStringLiteral("--force")},
                  true, Step::Tool::Generic, tr("Reading the latest firmware list"), {2}});
    steps.append({{QStringLiteral("fwupdmgr"), QStringLiteral("update"), QStringLiteral("-y"),
                   QStringLiteral("--no-reboot-check")},
                  true, Step::Tool::Generic, tr("Installing firmware updates"), {2}});
    return steps;
}

} // namespace gw
