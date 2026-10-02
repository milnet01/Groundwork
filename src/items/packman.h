// Packman, shared by the items that install from it: the codecs
// (ADR-0003) and, on Leap 16, Broadcom Wi-Fi
// (docs/research/2026-10-02-hardware.md). A Packman repository already
// configured, full or Essentials, is used as it is; otherwise Packman
// Essentials is added, never the full repository.
#pragma once

#include "core/item.h"

namespace gw {

// The alias of an enabled Packman repository, full or Essentials, on any
// mirror, read from /etc/zypp/repos.d; empty when there is none.
QString packmanAlias(const CheckContext &context);

// The steps that make Packman usable: add Essentials when none is
// configured, then refresh it, trusting its signing key. *alias is set
// to the repository to install from.
QList<Step> packmanSteps(const SystemIdentity &system, const CheckContext &context, QString *alias);

} // namespace gw
