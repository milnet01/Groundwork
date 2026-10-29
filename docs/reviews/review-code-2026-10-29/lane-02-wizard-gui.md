I read all 17 lane files in full, plus the contract sections the tail names. No CRITICAL or HIGH findings; four MEDIUM and several LOW. This was a depth pass.

**Line counts as read** (native `Read`, whole files): main.cpp 225 · wizard.h 191 · wizard.cpp 919 · carrypages.h 83 · carrypages.cpp 290 · savedsetuppage.h 31 · savedsetuppage.cpp 142 · runpage.h 58 · runpage.cpp 158 · itemrow.h 51 · itemrow.cpp 79 · switch.h 32 · switch.cpp 89 · themes.h 46 · themes.cpp 171 · askpassdialog.h 29 · askpassdialog.cpp 63.

**Present on arrival, before reading the subject:** the global `~/.claude/CLAUDE.md`, the user's email context, and a SessionStart hook note about Ants MCP. No project `CLAUDE.md`, no memory index, no git snapshot.

**Read outside the lane files, and why:**
- **Contract sections:** design.md §§ The shape, The levels, Carrying a setup over, What may depend on what, Where each sign is delivered; GRND-0079 §§ 3–6; GRND-0078 §§ 4.5, 4.7, 4.8, 5, 6; translations.md in full; the Idioms sections of qt.md and cpp.md.
- **To check claims (evidence-boundary carve-out):** one search of `src/carry` for how carried item titles are built, `src/items/claudecodeitem.h`, and two `.ts` entries (Arabic and Hebrew) for the new string.
- Every project-wide search excluded `tests/**`. Nothing under `tests/` was opened.

## Critical (0)

## High (0)

## Medium (4)

- [dim 3] `runpage.cpp:83` — `auto *label = new QLabel(tr("%1: waiting").arg(title), this);` — **Text from a saved file can render as HTML.**
  - **Where the text comes from.** A carried source's title is `"Software source: %1".arg(isolated(m_source.name))` (carrieditems.cpp:46). GRND-0078 § 4.7 gives a source's `name` no pattern; it only forbids control characters.
  - **Why it renders.** This label and the HINT, ACTION and summary labels (runpage.cpp:126, 142, 146) keep Qt's default `AutoText`. So does ItemRow's apply sentence (itemrow.cpp:20, also built from the name). `Qt::mightBeRichText` fires on the first tag-like `<…>` anywhere on the first line. The `isolated()` wrapper does not stop that.
  - **Effect.** A file from a stick (a trust boundary per GRND-0078 INV-3) can restyle or fake status text on the run page, for example make a failed step read "done", or load local images.
  - fix: `setTextFormat(Qt::PlainText)` on every RunPage label and on ItemRow's state, sentence and reason labels.

- [dim 2] `wizard.cpp:806-812` — `m_selection.remove(item->id()); ... m_saved.reset();` — **Clearing a saved setup leaves some of its choices switched on.**
  - `clearSavedSetup` removes only the carried ids.
  - Kept Groundwork items that `switchOnKept` turned on (INV-1) stay selected. So do dependencies pulled in by carried items, such as `system-update` via `carried-packages`.
  - This happens when the user goes back, switches "Start from a saved setup" off (or picks another job, or another file), and continues.
  - Those items then run, and their reasons are erased, so the rows no longer say why they are on. "clearSavedSetup forgets it" (wizard.h:127) does not hold.
  - fix: record the ids each saved setup switched on, including pulled ones, and switch them off in `clearSavedSetup`. Also clear the "You had this before." reasons.

- [dim 7] `wizard.cpp:899-901` — `QFile stop(stopFilePath(m_setup.stateDir)); (void)stop.open(QIODevice::WriteOnly); hide();` — **A failed stop request is ignored.**
  - If the stop file cannot be created (disk full, state folder not writable), the window still hides.
  - The Worker is never told to stop and runs every remaining item. The user believes they cancelled and is told nothing.
  - fix: check `open()`. On failure keep the window, and say the run cannot be stopped until the current item ends.

- [dim 16] `wizard.cpp:214` / `wizard.cpp:781,794` — `m_setup.readSetup(file, why, &skipped)` … `m_setup.checkItems(items)` — **Reading a saved file freezes the window.**
  - Both run synchronously on the window's thread inside `validatePage`.
  - That covers `tar --list`, `tar -xOf`, and then the carried checks: `rpm -q` of every kept package, `flatpak list`, and `tar --diff` over the settings archive. The archive can be several GiB on a USB stick.
  - The window freezes with no progress and no cancel, and the desktop may offer to kill it. The constructor's own comment (565) states the opposite rule for checks.
  - fix: do it off the window's thread, as the startup checks are, and finish the page change when it completes.

## Low / Info

- [dim 2] `savedsetuppage.cpp:56` — `delete findChild<QScrollArea *>();` — deletes a parented child by hand, against qt.md Idioms ("never manually `delete` a parented child"). fix: `deleteLater()`, after taking it out of `m_outer`.
- [dim 2] `wizard.cpp:76,263,316,394,413` — `class WelcomePage : public QWizardPage` (and the other four pages) — QObject subclasses without `Q_OBJECT`, against qt.md "on every `QObject` subclass, without exception". fix: add `Q_OBJECT` and `#include "wizard.moc"`. [tool: clazy-standalone] (its missing-qobject-macro check is not in the default set)
- [dim 3] `wizard.cpp:877` / `carrypages.cpp:278` — `QSaveFile file(restoreChoicesPath());` — the choices files get default permissions from the umask. They list the user's packages, settings and saved-file path. qt.md asks for owner-only permissions on config files. fix: `setPermissions(ReadOwner|WriteOwner)` before `commit()`.
- [dim 2] `switch.cpp:85` — `align | Qt::TextShowMnemonic` — names inserted as Switch text (carrypages.cpp:148, 162, 172; savedsetuppage.cpp:112–118) lose any `&`, which also becomes an Alt shortcut. Zypper source names and settings names can contain `&`. fix: escape `&` as `&&` when a name becomes switch text.
- [dim 13] `carrypages.cpp:27` / `savedsetuppage.cpp:71` — `QLocale().formattedDataSize(...)` / `QLocale().toString(created.toLocalTime(), ...)` — nothing in `src` calls `QLocale::setDefault`, so sizes and dates follow the system locale, not the language chosen on the first page. fix: set the default locale in `loadLanguage`, or pass `QLocale(language)`.
- [dim 13] `wizard.cpp:683` — `tr("%1 offers this too.").arg(m_rows.value(by)->toggle()->text())` — an item name inserted without `gw::isolated()`, which translations.md requires. The same holds at 653 and 660. fix: `isolated(...)`.
- [dim 13] `savedsetuppage.cpp:67` — `QDateTime::fromString(m.created, Qt::ISODate)` — a `created` value with no offset (a hand-made or foreign file) is read as local time. This code's own writer always adds `Z`. fix: treat a value with no offset as UTC.
- [dim 16] `main.cpp:110` / `wizard.cpp:570` — the checks thread iterates the static `gw::catalogue()`. That thread has no owner and nobody waits for it. Closing the window while the checks still run returns from `main`, so static destructors can run under the thread. fix: keep the thread, and `quit()`/`wait()` on shutdown.
- [dim 16] `wizard.cpp:863` / `899` — `restore-choices.json` and the stop file are fixed paths. Two copies running at once overwrite each other's choices, and closing one copy stops the other copy's Worker. No single-instance guard exists in main.cpp. fix: a lock file in the state folder.
- [dim 16] `carrypages.cpp:66` — `"/run/media/" + qEnvironmentVariable("USER")` — with `USER` unset, it lists `/run/media/` and offers a user's media folder as if it were a stick. fix: use the user name from `getpwuid(geteuid())`.
- [dim 2b] nothing found. Every promised entry point I checked has a caller in the lane files: SavedSetupPage, switchOnKept, startCarried, writeRestoreChoices, secretsArguments, holdsSecrets, usualSetupFolder, GatherPage, SavePage, and the Worker modes dispatched from main.cpp.
- [dim 12] N/A. The tail names no accessibility standard.
- [dim 4, 5, 8, 9, 10, 11, 15, 17] weighted out. Nothing seen in passing beyond the dim 16 thread item.
- INFO: I did not open `readSetupFile`, `carriedItems`, `saveProblem`, `sourceStartsOffWhy` or `manifestJson`. My reading of what they return comes from their call sites and the specs.

## Covered by spec and looks correct

- **Page routing.** `Wizard::nextId` matches GRND-0079 § 4.1 (the saved page comes after Welcome and before the level pages) and the carry and prepare page orders.
- **Saved setup page.** `makeRows` follows § 4.7's order and marks secret settings (§ 4.5). `startCarried` gives INV-8 sources their off reason. `switchOnKept` implements INV-1: once, only not-done items, licence items noted.
- **After the run.** The secrets offer follows § 4.8 and INV-9: it appears only after `runFinished`, deletion asks again naming the file, and nothing runs until a button is pressed.
- **Restore choices.** `writeRestoreChoices` and `runOrder` agree on which inner switches count (§ 4.6).
- **Carry pages.** GatherPage shows the secret warning when a secret is switched on (GRND-0078 INV-2). SavePage blocks Save on its problem text (INV-9) and names the largest rows (§ 4.5).
- **Entry.** main.cpp refuses to start as root, and builds every Worker command as an argument list, never through a shell.
- **Translations.** lupdate contexts are named in free functions (`Wizard::tr`, `"gw::Entry"`, `"gw::Themes"`). The Arabic and Hebrew translations of the new string do not start with `%1`.

## Open questions

- The welcome page's live-system text (wizard.cpp:166) shows only when no job is offered. main.cpp always offers Prepare a drive, so a live system sees no explanation of why only that job is offered. Is that intended?
- A latent bug: the also-offered loop (wizard.cpp:676–687) overwrites any reason set directly. That includes INV-8's off reason and "You had this before." Today only `dev-claude-code` declares `alsoOfferedBy`, and it accepts no licence, so this cannot happen yet.
- Where my brief disagreed with the shared context, I followed the brief. The shared context says the contract docs are "provided in full; do not re-read them", but they were not in the packet. I read the sections the tail names.

## 3 items to fix first

1. **Rich text from a saved file (runpage.cpp:83 and the other labels).** It is the only finding at a trust boundary, and a crafted file can fake what the run page says about steps that change the system.
2. **`clearSavedSetup` leaving items on (wizard.cpp:806).** Items the user dropped along with the file still run as root, on a common back-and-forth path.
3. **Ignored stop-file failure (wizard.cpp:900).** A cancel the user cannot see fail lets the run carry on changing the system.
