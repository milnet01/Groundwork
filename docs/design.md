# Groundwork — Design

> **Purpose — so the shape is decided once, and anyone can tell where a
> new piece of work belongs and what it is allowed to touch.**

**This document is a gate.** Work is not broken into items until it is
agreed — `~/.claude/workflow.md` § 4 says when it passes.

**Status:** agreed (2026-10-02). The user delegated the approval to
Claude, which gave it after the independent review reached its cap.

What it serves: `docs/discovery.md`, signs S1–S4.

## The shape, in one paragraph

Groundwork is one program with four modes. Started plainly, it opens a
**wizard**: one page per level, then a review page, then a run page. The
wizard never has root. When the user presses Apply, it starts a second
copy of itself in **worker** mode. The worker asks for the password once,
does the root work, and reports progress as marker lines on its output.
The worker also runs on its own in a terminal, so a broken desktop can
still use it; there, with no display, `sudo` asks for the password on
the terminal. A **check** mode prints every item's state in a terminal
and changes nothing. An **askpass** mode is the password box `sudo`
calls, so the app needs no desktop's own password helper.

## The levels

The wizard shows the levels in this order, one page each. An item
starts switched on only in Essentials, and only if its check says it is
not done (S1, S4). This replaces the kickoff rule that near-universal
extras start on (`docs/brief.md`): what is near-universal is placed in
Essentials. Every item's check runs before any page is shown, so
each row already says "already done", "not done", "not needed here" or
"couldn't tell".

**Bringing the system up to date is a preparation, not a goal.** Every
item that installs packages depends on it, so the installs meet a
current system. It is switched on only through that dependency. Its row
says whether updates are waiting, and S1 does not count it. Keeping a
machine up to date afterwards is OneUp's job.

**Dependencies are kept by both sides.** Switching an item on in the
Wizard switches on what it depends on, and the row says why. The Worker,
given an item whose dependency is neither done nor in its list, skips
it and says why. **Core owns the rule** for which items start switched
on and for closing dependencies, over the items it is given, so the
Wizard and the Worker apply the same one.

1. **Essentials** — what a desktop user needs for things to work.
   Bring the system up to date; media codecs, including video in the
   browser; Flatpak and Flathub. Hardware support, offered only where
   the hardware is found and lacks it: the NVIDIA driver, sound
   firmware, Broadcom Wi-Fi.
2. **System setup** — that the system can recover and is protected.
   Btrfs snapshots, the firewall, firmware updates, and laptop power
   settings where there is a battery.
3. **Configuration** — choices a new install asks of its owner. The
   computer's name; remote login over SSH; the clock setting, where
   Windows is also installed.
4. **Nice to have** — extras, each its own toggle. Everyday apps,
   gaming, developer tools, a backup tool for the user's own files,
   Microsoft-compatible fonts, and handy command-line programs.

Which needs are common comes from the sources in
`docs/research/2026-10-02-sources.md`. Desktop look and feel is left
out: it differs between desktops, and each desktop's own settings
cover it.

After a run, if OneUp is installed, the last page offers to open it.

The list of items is one catalogue file in Items, not code spread
across the wizard. Adding an item means adding one item file and one
catalogue line. Each item's commands are taken from current openSUSE
sources when that item is built, and its roadmap item cites them. None
are recorded here.

## Which systems it runs on

It reads `ID` and `VERSION_ID` from `/etc/os-release`, never `NAME` or
`ID_LIKE`.

- `opensuse-tumbleweed` and `opensuse-slowroll` — supported, as the
  rolling family.
- `opensuse-leap` with `VERSION_ID` 16 or later — supported.
- Anything else — the wizard says in plain English that this system is
  not supported, and stops. That includes Leap 15, Leap Micro, MicroOS
  and other distributions. Leap Micro and MicroOS update through
  `transactional-update`, which every item here would get wrong.

An item that does not exist on a system is "not needed here", not an
error. For example, `fetchmsttfonts` was not found in Leap 16.0's main
repository.

## The parts

Every path is under `src/` unless it says otherwise.

| Part | Responsible for | Files |
|---|---|---|
| **Core** | Reading the system's identity; the item interface; running a read-only command with a time limit; reading a system file; running the checks it is given; the default selection and dependency closure; the marker format | `core/` |
| **Items** | One file per item: its check, a plain-English sentence saying what its apply would do, and the steps its apply would run; the catalogue listing every item | `items/` |
| **Worker** | Getting root once and keeping it; running steps in order; stopping only between items; the log | `worker/` |
| **Wizard** | The pages, the rows, reading markers from the worker, the askpass box | `gui/` |
| **Entry** | Choosing the mode: from the command line, or, for askpass, from an environment variable the Worker sets, because `SUDO_ASKPASS` names a program path and carries no option (`man sudo`, `-A`); check mode, which prints Core's results for the catalogue's checks | `main.cpp` |
| **Tests** | Unit tests and the fake-command scenarios | `tests/` |
| **Packaging** | The AppImage build | `packaging/appimage/` |

The marker format is OneUp's: `@@NAME@@|field|field`, one per line, on
standard output.

**The Worker's interface** is one reference file, created by the
Worker's roadmap item: its markers, its command line, and where the
stop file lives. The Worker takes the ids of the items to run on its
command line. Given none, it runs the items the Wizard would start
switched on. The Wizard never starts it with an empty selection: Apply
is unavailable until an item is switched on.

## What may depend on what

- **Core depends on Qt Core only.** It never includes anything from
  Items, Worker or Wizard.
- **Items depend on Core only.** An item never runs a command or opens
  a file itself. Its check names the read-only commands and system
  files it needs, and how to read them; Core runs and reads them. Its
  apply returns a list of steps, which the Worker runs.
- **Worker and Wizard depend on Core and Items, never on each other.**
  They meet only through a child process: the Wizard starts the Worker,
  reads its markers, and asks it to stop by writing a file. No shared
  memory, no signals.
- **Only the Worker's privilege file calls `sudo`.** Every root step goes
  through it. Nothing else may start a root process.
- **No shell.** Every command is a fixed argument list. Nothing builds a
  command string for `sh -c`.
- **Entry may depend on every part**; no part depends on Entry.
- **The Wizard refuses to start as root.**

## What every part does the same way

- **Check results** are one of: done, not done, not needed here,
  couldn't tell. "Couldn't tell" carries a reason and is never shown as
  done. A skipped repository is "couldn't tell", not "up to date".
- **Step results** are ok, skipped or failed, with a detail line. A
  failed item causes only the items that depend on it to be skipped,
  each saying why; all others still run. Each item states what it
  depends on.
- **The Worker re-checks before it applies.** It runs an item's check
  immediately before the item's steps, and applies only on "not done".
  Any other result skips the item: "skipped, already done", or skipped
  with the reason it couldn't tell.
- **zypper's exit codes in an apply step** are read with OneUp's rule:
  0, 100–103 and 106 are success. 106 also means a repository was
  skipped, and the user is told which. 103 means zypper updated itself,
  so the step is run once more to finish (`man zypper`, EXIT CODES). In
  a check, 106 gives "couldn't tell".
- **Stopping** is cooperative, between items. The Worker never signals a
  running zypper. Closing the window asks the Worker to stop; the Worker
  survives its output being closed and finishes the current item.
- **Root** is asked for once per run and kept alive the way OneUp's
  engine does it: a child that re-runs `sudo -n -v` and dies with the
  Worker. The password box shows `sudo`'s own prompt, which names whose
  password it wants: root's, on Tumbleweed as measured 2026-10-02.
  With a display, `sudo -A` shows the app's own
  password box; without one, `sudo` asks on the terminal. No root
  command runs inside a pipe or a captured subshell.
- **Logging** goes to one file per run under the user's state directory,
  and the Worker's output is mirrored there.
- **Persistence:** none, beyond the logs and the window's own settings.
  Every run starts from fresh checks.
- **Text** is plain English, written for someone new to openSUSE. The
  window follows the system's font size and colour scheme, and works at
  large font sizes.
- **Tests never touch the real system.** Commands are replaced by fakes
  placed first on `PATH`. Core's file reader and every state path take a
  root directory that tests redirect.

## Where each sign is delivered

| Sign | Delivered by |
|---|---|
| S1 — a set-up machine changes nothing | Items' checks, run by Core; the Worker's re-check before apply |
| S2 — one button and one password on a fresh system, video plays | The whole: the Essentials items, the Worker, the Wizard, the AppImage |
| S3 — each row says what it would do | Each item's plain-English sentence; the Wizard's rows |
| S4 — levels in order, any item switched on or off, only those run | The Wizard's pages and toggles; the Worker runs only the items it is given |

## The stack, and what it rules out

- **C++20 and Qt 6 (Core and Widgets), built with CMake, tested with Qt
  Test under CTest** — the user's choice, and Qt has a wizard component
  (ADR-0001). Runner-up: Python and PySide6, copying OneUp.
- **One AppImage** carries the program and its Qt libraries (ADR-0002).
  It is built on the oldest supported system, so its libraries are not
  newer than any supported system's.
- **Rules out:**
  - a polkit policy or any file installed system-wide, since an AppImage
    installs none;
  - YaST, which Leap 16.0 removed;
  - Python, and any library that is neither bundled in the AppImage nor
    part of a base install;
  - a window running as root;
  - systems that update through `transactional-update`.

## Close calls

- [ADR-0001](decisions/ADR-0001-cpp-and-qt.md) — C++ and Qt, not Python.
- [ADR-0002](decisions/ADR-0002-appimage-in-0-1-0.md) — the AppImage
  ships in 0.1.0.
- [ADR-0003](decisions/ADR-0003-packman-essentials.md) — codecs come
  from Packman's Essentials repository, not the full one.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-10-02 | 2, each holding every question | 2 | 4 | 5 | — | 11 verified, 11 fixed; 3 dismissed as not changing the design (the keep-alive mechanism, which password `sudo` asks for, Slowroll's Packman tree: each settled by its item). Before dispatch, building the packet found the research record's rootless firewall check false; fixed in 3aa443f, outside the subject. Discovery's S1, S2 and supported systems narrowed to match, per `workflow.md` § 4. |
| 2 | 2026-10-02 | 2, each holding every question | 1 | 4 | 4 | — | 9 verified, 9 fixed: the catalogue moved from Core to Items; the update item starts on only with another item, keeping S1; the Worker's command line, default and stop file have one owner; dependencies are kept by Wizard and Worker; zypper's rule is scoped to apply steps, 103 re-runs (`man zypper`); `ID_LIKE` never read; askpass mode is chosen by an environment variable (`man sudo`); one collision this loop's own fix created (the Worker's default) fixed before commit. 3 dismissed as settled by an item: the keep-alive mechanism, FUSE on a fresh install (ADR-0002 already requires it), the NVIDIA key prompt at boot. |
| 3 | 2026-10-02 | 2, each holding every question | 0 | 3 | 5 | — | 8 verified, 8 fixed: the update item is a dependency of every installing item and S1 does not count it (discovery's S1 narrowed to match); a failed item skips only its dependants; the Wizard never sends an empty selection; Core owns the default selection and dependency closure; the Worker applies only on "not done"; the keep-alive follows OneUp's engine; the password box shows `sudo`'s prompt (root's password, measured); the library rule allows what the AppImage bundles. Askpass by environment variable measured: `sudo -A` passes the caller's variable and the prompt as the first argument. At the ADR cap: this loop's fixes are read by no lane. Calm cap: of the 8, 5 landed on text loops 1–2 wrote (update rule, dependencies, Worker default, keep-alive, default selection), all unpropagated consequences of loop 2's update decision rather than repairs of repairs. Second share: the whole document was new in this gate, so every finding is inside the armed span by construction. |
