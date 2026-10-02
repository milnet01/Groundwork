// Runs the checks it is given, in order, and keeps every result.
#pragma once

#include "item.h"

#include <QHash>
#include <QList>

namespace gw {

using CheckResults = QHash<QString, CheckResult>; // keyed by Item::id()

CheckResults runChecks(const QList<const Item *> &items, const CheckContext &context);

} // namespace gw
