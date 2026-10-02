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

QString stateText(CheckState state)
{
    switch (state) {
    case CheckState::Done: return QCoreApplication::translate("gw::CheckRunner", "already done");
    case CheckState::NotDone: return QCoreApplication::translate("gw::CheckRunner", "not done");
    case CheckState::NotNeeded: return QCoreApplication::translate("gw::CheckRunner", "not needed here");
    case CheckState::CouldNotTell: return QCoreApplication::translate("gw::CheckRunner", "couldn't tell");
    }
    return {};
}

} // namespace gw
