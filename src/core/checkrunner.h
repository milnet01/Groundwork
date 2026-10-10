// Runs the checks it is given, in order, and keeps every result.
#pragma once

#include "item.h"

#include <QHash>
#include <QList>

namespace gw {

using CheckResults = QHash<QString, CheckResult>; // keyed by Item::id()

CheckResults runChecks(const QList<const Item *> &items, const CheckContext &context);

// The words a row shows for a state: "already done" and the others.
QString stateText(CheckState state);

// A row's state line: the state's words, then " — " and the detail. A
// preparation that is not done shows the detail alone, since it is always
// "not done" and the word beside "no updates were waiting" reads as a
// contradiction (GRND-0051).
QString stateLine(const Item &item, const CheckResult &result);

} // namespace gw
