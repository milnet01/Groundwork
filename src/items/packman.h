// Packman, shared by the items that install from it: the codecs
// (ADR-0004) and, on Leap 16, Broadcom Wi-Fi
// (docs/research/2026-10-02-hardware.md). The full Packman repository
// already configured is used as it is; otherwise it is added. An
// Essentials repository is left in place and never installed from.
#pragma once

#include "core/item.h"

namespace gw {

// The alias of the first enabled repository in /etc/zypp/repos.d whose
// baseurl contains urlPart (case-insensitive); empty when there is none.
QString repositoryAlias(const CheckContext &context, const QString &urlPart);

// The alias of an enabled full Packman repository, on any mirror; empty
// when there is none. Essentials does not count.
QString packmanAlias(const CheckContext &context);

// The steps that make Packman usable: add the full repository when none
// is configured, then refresh it, trusting its signing key. *alias is
// set to the repository to install from.
QList<Step> packmanSteps(const SystemIdentity &system, const CheckContext &context, QString *alias);

} // namespace gw
