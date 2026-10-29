// Where Groundwork keeps run state. The Wizard and the Worker both need the
// stop file and never depend on each other, so the paths live in Core.
// Tests redirect them through XDG_STATE_HOME.
#pragma once

#include <QString>

class QLockFile;

namespace gw {

// $XDG_STATE_HOME/groundwork, or ~/.local/state/groundwork.
QString defaultStateDir();

// The file the Wizard writes to ask the Worker to stop between items.
QString stopFilePath(const QString &stateDir);

// Makes the state folder, or narrows one made before, for the user alone:
// it holds their choices, saved-setup lists and logs (GRND-0090).
bool makeStateDir(const QString &dir);

// The Wizard's lock, held while a window is open, since two would share
// the choices and stop files.
QString wizardLockPath(const QString &stateDir);
// Takes the lock; true only when another live copy already holds it. A
// lock that cannot be made at all does not stop the window opening.
bool anotherCopyHolds(QLockFile &lock);

} // namespace gw
