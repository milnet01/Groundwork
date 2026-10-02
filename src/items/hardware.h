// Reading the machine's hardware without root, for the hardware items.
#pragma once

#include "core/item.h"

#include <QList>

namespace gw {

struct PciDevice
{
    QString slot;   // e.g. 0000:00:1f.3
    QString vendor; // e.g. 0x8086
    QString device; // e.g. 0x4360
    QString cls;    // e.g. 0x028000
    QString driver; // the bound driver's name; empty when none
};

// Every device under /sys/bus/pci/devices.
QList<PciDevice> pciDevices(const CheckContext &context);

// Secure Boot is on: the last byte of the SecureBoot EFI variable, which
// /sys/firmware/efi/efivars lets anyone read (efivarfs: 4 bytes of
// attributes, then the value; docs/research/2026-10-02-hardware.md).
bool secureBootOn(const CheckContext &context);

} // namespace gw
