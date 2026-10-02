// A systemd service's state, read without root. Measured 2026-10-02:
// `systemctl is-enabled` prints enabled (0), disabled (1) or not-found
// (4); `systemctl is-active` prints active (0) or inactive (3, or 4 for a
// unit that does not exist).
#pragma once

#include "core/item.h"

namespace gw {

struct ServiceState
{
    bool known = false;   // systemctl answered
    bool found = false;   // the unit exists
    bool enabled = false; // starts at boot
    bool active = false;  // running now
};

ServiceState readService(const CheckContext &context, const QString &unit);

} // namespace gw
