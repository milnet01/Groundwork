# Groundwork — Design

> **Purpose — so the shape is decided once, and anyone can tell where a
> new piece of work belongs and what it is allowed to touch.**

**This document is a gate.** Work is not broken into items until it is
agreed — `~/.claude/workflow.md` § 2. It passes when someone can take any
item off the queue and say which part it belongs in and what it may
touch.

**Status:** draft, 2026-10-02 — awaiting the user's agreement.

What it serves: `docs/discovery.md`, signs S1–S4.

## The shape, in one paragraph

Groundwork is one program with four modes. Started plainly, it opens a
**wizard**: one page per level, then a review page, then a run page. The
wizard never has root. When the user presses Apply, it starts a second
copy of itself in **worker** mode. The worker asks for the password once,
does the root work, and reports progress as marker lines on its output.
The worker also runs on its own in a terminal, so a broken desktop can
still use it. A **check** mode prints every item's state in a terminal
and changes nothing. An **askpass** mode is the password box `sudo`
calls, so the app needs no desktop's own password helper.

## The levels

The wizard shows the levels in this order, one page each. An item
starts switched on only in Essentials, and only if its check says it is
not done (S1, S4). Every item's check runs before any page is shown, so
each row already says "already done", "not done", "not needed here" or
"couldn't tell".

1. **Essentials** — what a desktop user needs for things to work.
   Bring the system up to date; media codecs; Flatpak and Flathub; the
   graphics driver, offered only where the card needs one.
2. **System setup** — that the system can recover and is protected.
   Btrfs snapshots, firmware updates, the firewall.
3. **Configuration** — choices a new install asks of its owner. The
   computer's name, and remote login over SSH.
4. **Nice to have** — extras. Microsoft-compatible fonts, and a short
   list of handy programs, each its own toggle.

After a run, if OneUp is installed, the last page offers to open it.

The list of items is data in one file, not code spread across the
wizard. Adding an item means adding one item file and one catalogue
line. Each item's commands are taken from current openSUSE sources when
that item is built, and its roadmap item cites them. None are recorded
here.

## Which systems it runs on

It reads `ID` and `VERSION_ID` from `/etc/os-release`, never `NAME` and
never `ID_LIKE` first.

- `opensuse-tumbleweed` and `opensuse-slowroll` — supported, as the
  rolling family.
- `opensuse-leap` with `VERSION_ID` 16 or later — supported.
- Anything else — the wizard says in plain English that this system is
  not supported, and stops. That includes Leap 15, Leap Micro, MicroOS
  and other distributions. Leap Micro and MicroOS update through
  `transactional-update`, which every item here would get wrong.

An item that does not exist on a system is "not needed here", not an
error. For example, `fetchmsttfonts` is not in Leap 16.0's repositories.

## The parts

Every path is under `src/` unless it says otherwise.

| Part | Responsible for | Files |
|---|---|---|
| **Core** | Reading the system's identity; the item interface; the catalogue; running a read-only command with a time limit; the marker format | `core/` |
| **Items** | One file per item: its check, and the steps its apply would run | `items/` |
| **Worker** | Getting root once and keeping it; running steps in order; stopping only between items; the log | `worker/` |
| **Wizard** | The pages, the rows, reading markers from the worker, the askpass box | `gui/` |
| **Entry** | Choosing the mode from the command line | `main.cpp` |
| **Tests** | Unit tests and the fake-command scenarios | `tests/` |
| **Packaging** | The AppImage build | `packaging/appimage/` |

The marker format is OneUp's: `@@NAME@@|field|field`, one per line, on
standard output. Groundwork's own list of markers lives in
a reference file the worker's roadmap item creates.

## What may depend on what

- **Core depends on Qt Core only.** It never includes anything from
  Items, Worker or Wizard.
- **Items depend on Core only.** An item never runs a command itself.
  Its check returns the read-only commands to run and how to read their
  output. Its apply returns a list of steps. Core or Worker runs them.
- **Worker and Wizard depend on Core and Items, never on each other.**
  They meet only through a child process: the Wizard starts the Worker,
  reads its markers, and asks it to stop by writing a file. No shared
  memory, no signals.
- **Only the Worker's privilege file calls `sudo`.** Every root step goes
  through it. Nothing else may start a root process.
- **No shell.** Every command is a fixed argument list. Nothing builds a
  command string for `sh -c`.
- **The Wizard refuses to start as root.**

## What every part does the same way

- **Check results** are one of: done, not done, not needed here,
  couldn't tell. "Couldn't tell" carries a reason and is never shown as
  done. A skipped repository is "couldn't tell", not "up to date".
- **Step results** are ok, skipped or failed, with a detail line. A
  failed item does not stop the items after it, unless one depends on
  it. Each item states what it depends on.
- **zypper's exit codes** are read with OneUp's rule. 0 and 100–103 are
  success. 106 means a repository was skipped and is surfaced to the
  user.
- **Stopping** is cooperative, between items. The Worker never signals a
  running zypper.
- **Root** is asked for once per run, through `sudo -A`, and kept alive
  by a helper that dies with the Worker. No root command runs inside a
  pipe or a captured subshell.
- **Logging** goes to one file per run under the user's state directory,
  and the Worker's output is mirrored there.
- **Persistence:** none, beyond the logs and the window's own settings.
  Every run starts from fresh checks.
- **Text** is plain English, written for someone new to openSUSE. The
  window follows the system's font size and colour scheme, and works at
  large font sizes.
- **Tests never touch the real system.** Commands are replaced by fakes
  placed first on `PATH`, and every state path is redirected.

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
  - Python, and any library a base install might not have;
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
