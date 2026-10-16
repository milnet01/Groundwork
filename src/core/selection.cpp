#include "selection.h"

namespace gw {

const Item *Catalogue::find(const QString &id) const
{
    for (const Item *item : m_items)
        if (item->id() == id)
            return item;
    return nullptr;
}

QStringList Catalogue::dependencies(const Item &item) const
{
    QStringList deps = item.dependsOn();
    if (item.installsPackages()) {
        for (const Item *other : m_items)
            if (other->isPreparation() && other != &item && !deps.contains(other->id()))
                deps.append(other->id());
    }
    return deps;
}

Selection Catalogue::defaultSelection(const CheckResults &results) const
{
    Selection selection;
    for (const Item *item : m_items) {
        if (item->level() == Level::Essentials && !item->isPreparation()
            && !item->acceptsLicence() && results.value(item->id()).state == CheckState::NotDone)
            switchOn(selection, item->id(), results);
    }
    return selection;
}

QStringList Catalogue::switchOn(Selection &selection, const QString &id,
                                const CheckResults &results, QStringList *switchedOff) const
{
    QStringList pulled;
    QStringList on;
    QStringList pending{id};
    bool first = true;
    while (!pending.isEmpty()) {
        const QString current = pending.takeFirst();
        const Item *item = find(current);
        if (!item || selection.contains(current))
            continue;
        if (!first) {
            const CheckState state = results.value(current).state;
            if (state == CheckState::Done || state == CheckState::NotNeeded)
                continue;
            pulled.append(current);
        }
        first = false;
        selection.insert(current);
        on.append(current);
        pending.append(dependencies(*item));
    }
    for (const QString &current : std::as_const(on)) {
        for (const QString &included : find(current)->includes()) {
            if (!selection.contains(included))
                continue;
            switchOff(selection, included);
            if (switchedOff)
                switchedOff->append(included);
        }
    }
    return pulled;
}

QStringList Catalogue::switchOff(Selection &selection, const QString &id) const
{
    QStringList pulled;
    QStringList pending{id};
    bool first = true;
    while (!pending.isEmpty()) {
        const QString current = pending.takeFirst();
        if (!selection.remove(current))
            continue;
        if (!first)
            pulled.append(current);
        first = false;
        for (const Item *other : m_items)
            if (dependencies(*other).contains(current))
                pending.append(other->id());
    }
    return pulled;
}

QStringList Catalogue::runOrder(const Selection &selection) const
{
    QStringList order;
    for (const Item *item : m_items)
        if (selection.contains(item->id()))
            order.append(item->id());
    return order;
}

QStringList Catalogue::orderProblems() const
{
    QStringList problems;
    QSet<QString> seen;
    for (const Item *item : m_items) {
        for (const QString &dep : dependencies(*item)) {
            const Item *target = find(dep);
            if (!target)
                problems.append(item->id() + QStringLiteral(" depends on unknown ") + dep);
            else if (target->acceptsLicence())
                problems.append(item->id() + QStringLiteral(" depends on licence item ") + dep);
            else if (!seen.contains(dep))
                problems.append(item->id() + QStringLiteral(" is listed before ") + dep);
        }
        for (const QString &included : item->includes())
            if (!find(included))
                problems.append(item->id() + QStringLiteral(" includes unknown ") + included);
        seen.insert(item->id());
    }
    return problems;
}

} // namespace gw
