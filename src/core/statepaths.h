// Where Groundwork keeps run state. The Wizard and the Worker both need the
// stop file and never depend on each other, so the paths live in Core.
// Tests redirect them through XDG_STATE_HOME.
#pragma once

#include <QString>

namespace gw {

// $XDG_STATE_HOME/groundwork, or ~/.local/state/groundwork.
QString defaultStateDir();

// The file the Wizard writes to ask the Worker to stop between items.
QString stopFilePath(const QString &stateDir);

} // namespace gw
