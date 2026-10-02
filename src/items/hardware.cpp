#include "hardware.h"

namespace gw {
namespace {
const QString kDevices = QStringLiteral("/sys/bus/pci/devices");
} // namespace

QList<PciDevice> pciDevices(const CheckContext &context)
{
    QList<PciDevice> devices;
    for (const QString &slot : context.entries(kDevices)) {
        const QString dir = kDevices + QLatin1Char('/') + slot + QLatin1Char('/');
        auto read = [&](const char *name) {
            return QString::fromLatin1(context.readFile(dir + QLatin1String(name)).value_or(QByteArray())).trimmed();
        };
        devices.append({slot, read("vendor"), read("device"), read("class"),
                        context.linkTargetName(dir + QStringLiteral("driver"))});
    }
    return devices;
}

bool secureBootOn(const CheckContext &context)
{
    const auto data = context.readFile(
        QStringLiteral("/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c"));
    return data && data->size() >= 5 && data->at(4) == 1;
}

} // namespace gw
