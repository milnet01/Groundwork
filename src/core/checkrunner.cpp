#include "checkrunner.h"

#include <QCoreApplication>

namespace gw {

CheckResults runChecks(const QList<const Item *> &items, const CheckContext &context)
{
    CheckResults results;
    for (const Item *item : items) {
        CheckResult result = item->check(context);
        // "Couldn't tell" always says why, so a row never shows a bare unknown.
        if (result.state == CheckState::CouldNotTell && result.detail.isEmpty())
            result.detail = QCoreApplication::translate("gw::CheckRunner", "The check gave no reason.");
        results.insert(item->id(), result);
    }
    return results;
}

} // namespace gw
