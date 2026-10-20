// The items a saved file's kept parts become (GRND-0079;
// docs/specs/GRND-0079-start-from-saved-setup.md § 4.2): a software source
// each, then the programs, the Flatpak apps and the settings, one item
// each. They come after every Groundwork item, and the Wizard or the
// Worker that builds them keeps them alive.
#pragma once

#include "carry.h"

#include <memory>
#include <vector>

namespace gw {

inline const QString kCarriedPackagesId = QStringLiteral("carried-packages");
inline const QString kCarriedFlatpaksId = QStringLiteral("carried-flatpaks");
inline const QString kCarriedSettingsId = QStringLiteral("carried-settings");
// A source's item id: this, then its alias.
inline const QString kCarriedSourcePrefix = QStringLiteral("carried-source:");
// Where a source is refreshed before it is moved into /etc/zypp/repos.d,
// so one that fails is never added (spec 4.3, measured 2026-10-20).
inline const QString kStagedReposDir = QStringLiteral("/run/groundwork/repos.d");

// True for an id only a saved file gives; no Groundwork id is one (INV-3).
bool isCarriedId(const QString &id);

struct CarriedItems
{
    std::vector<std::unique_ptr<Item>> owned; // in run order
    QStringList keptItems;    // kept ids this catalogue has
    QStringList unknownItems; // kept ids it lacks, named on the page
};

// The items for the kept parts of manifest, read from file. A part with
// nothing kept builds no item. Settings go to the home folder,
// QDir::homePath() when this is called.
CarriedItems carriedItems(const Manifest &kept, const QString &file, const Catalogue &catalogue);
// The catalogue's items, then the carried ones.
Catalogue withCarried(const Catalogue &catalogue, const CarriedItems &carried);

// Why a kept source starts switched off: its signatures go unchecked, or
// the file was saved on another system (INV-8). Empty when it starts on.
QString sourceStartsOffWhy(const ZypperSource &source, const QString &savedOn, const QString &thisSystem);

// A .repo file holding source as the old machine had it (spec 4.3).
QByteArray repoFileText(const ZypperSource &source);

// tar's arguments to restore settings into home (spec 4.5); the paths go
// on standard input, NUL-separated, as createInput writes them.
QStringList extractArguments(const QString &file, const QString &home);

// Plain words saying which entry of chosen the file's manifest lacks;
// empty when it lacks none (INV-2).
QString notInFile(const Manifest &chosen, const Manifest &file);

} // namespace gw
