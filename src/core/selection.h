// Which items start switched on, what switching one on or off pulls with
// it, and the order the Worker runs them in. Core owns this one rule so the
// Wizard and the Worker apply the same one (docs/design.md, The levels).
#pragma once

#include "checkrunner.h"
#include "item.h"

#include <QList>
#include <QSet>
#include <QStringList>

namespace gw {

using Selection = QSet<QString>; // item ids

class Catalogue
{
public:
    explicit Catalogue(QList<const Item *> items) : m_items(std::move(items)) {}

    const QList<const Item *> &items() const { return m_items; }
    const Item *find(const QString &id) const;

    // What an item depends on: what it declares, plus every preparation
    // when it installs packages.
    QStringList dependencies(const Item &item) const;

    // Essentials whose check says "not done", other than preparations and
    // items that accept a licence, plus what they depend on.
    Selection defaultSelection(const CheckResults &results) const;

    // Switch an item on, with each dependency not already done or not
    // needed. Returns the ids switched on because something needed them.
    QStringList switchOn(Selection &selection, const QString &id,
                         const CheckResults &results) const;

    // Switch an item off, with everything that depends on it. Returns the
    // ids switched off because they needed it.
    QStringList switchOff(Selection &selection, const QString &id) const;

    // The selected ids in catalogue order, which is the Worker's run order.
    QStringList runOrder(const Selection &selection) const;

    // Items listed before something they depend on, depending on an id the
    // catalogue lacks, or depending on an item that accepts a licence. Empty
    // when the catalogue is well formed.
    QStringList orderProblems() const;

private:
    QList<const Item *> m_items;
};

} // namespace gw
