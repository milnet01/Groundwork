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
not done (S1, S4). An item whose apply accepts a licence for the user
never starts switched on, at any level; the user switches it on, and
its row names the licence. No item depends on one, so switching on
another item never switches it on. This replaces the kickoff rule that near-universal
extras start on (`docs/brief.md`): what is near-universal is placed in
Essentials. Every item's check runs before any page is shown, so
each row already says "already done", "not done", "not needed here" or
"couldn't tell".

**Bringing the system up to date is a preparation, not a goal.** Every
item that installs packages depends on it, so the installs meet a
current system. It never starts switched on by itself: switching on an
item that installs packages switches it on, and the user may also
switch it on alone. Its row says whether updates are waiting, and S1 does not count it. Keeping a
machine up to date afterwards is OneUp's job.

**Dependencies are kept by both sides.** Switching an item on in the
Wizard switches on what it depends on, and switching one off switches
off what needs it; the rows say why. The Worker, given an item whose
dependency is neither done nor in its list, skips it and says why. **Core owns the rule** for which items start switched
on and for closing dependencies, over the items it is given, so the
Wizard and the Worker apply the same one.

1. **Essentials** — what a desktop user needs for things to work.
   Bring the system up to date; media codecs, including video in the
   browser; Flatpak and Flathub. Hardware support, offered only where
   the hardware is found and lacks it: the NVIDIA driver, sound
   firmware, Broadcom Wi-Fi.
2. **System setup** — that the system can recover and is protected.
   Btrfs snapshots, the firewall, firmware updates, and laptop power
   settings where there is a battery. RAM protection, which closes the
   most memory-hungry app before the desktop freezes. The emergency
   keyboard escape, which restarts a frozen computer cleanly. A smoother
   scheduler for spinning hard drives, where one is found. A calmer wake
   from hibernation, with missed maintenance jobs spread out. A 1 GB
   limit on the system log, which deletes the oldest entries first. A
   3-second wait at the boot menu.
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
- Anything else — not supported, in every mode: the wizard says so in
  plain words and stops; the Worker and check mode print it and exit
  non-zero. That includes Leap 15, Leap Micro, MicroOS
  and other distributions. Leap Micro and MicroOS update through
  `transactional-update`, which every item here would get wrong.

An item that does not exist on a system is "not needed here", not an
error. For example, `fetchmsttfonts` was not found in Leap 16.0's main
repository.

## The parts

Source files are under `src/`; tests and packaging are at the top level.

| Part | Responsible for | Files |
|---|---|---|
| **Core** | Reading the system's identity; the item interface; running a read-only command with a time limit; reading a system file; running the checks it is given; the default selection and dependency closure; the marker format | `core/` |
| **Items** | One file per item: its check, a plain sentence saying what its apply would do, whether that apply accepts a licence, and the steps its apply would run; the catalogue listing every item | `items/` |
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
command line, with the language to speak. Given no items, it runs the
items the Wizard would start switched on; given no language, it uses
the system's. It runs items in catalogue order, whatever order the ids
arrive in, and the catalogue lists every item after what it depends on.
The Wizard never starts it with an empty selection: Apply is
unavailable until an item is switched on. The Worker translates the
text it produces itself.

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
  A step may carry text for its command's standard input; a file is
  written that way, by `dd of=<path> status=none`, so its text stays
  out of the log.
- **Entry may depend on every part**; no part depends on Entry.
- **The Wizard refuses to start as root.**

## What every part does the same way

- **Check results** are one of: done, not done, not needed here,
  couldn't tell. "Couldn't tell" carries a reason and is never shown as
  done. A skipped repository is "couldn't tell", not "up to date" —
  except in the update item's check, which reports the updates the
  other repositories offer and names the one it skipped, so one
  unreachable repository does not block every install.
- **Step results** are ok, skipped or failed, with a detail line. An
  item whose dependency failed, or was skipped for any reason but
  "already done" or "not needed here", is skipped too, saying why; all others still run.
  Each item states what it depends on.
- **The Worker re-checks before it applies.** It runs an item's check
  immediately before the item's steps, and applies only on "not done".
  Any other result skips the item: "skipped, already done", "skipped,
  not needed here", or skipped with the reason it couldn't tell.
- **zypper's exit codes in an apply step** are read with OneUp's rule:
  0, 100–103 and 106 are success. 106 also means a repository was
  skipped, and the user is told which. 103 means zypper updated itself,
  so the step is run once more to finish (`man zypper`, EXIT CODES). In
  a check, 106 gives "couldn't tell", except in the update item's
  check (Check results, above).
- **Stopping** is cooperative, between items. The Worker never signals a
  running zypper. Closing the window during a run asks the Worker to
  stop and hides the window; the Wizard's process stays until the
  Worker has finished the current item and exited, so neither Qt nor
  the AppImage's mount ends the Worker early. The Worker also survives
  its output being closed.
- **Root** is asked for once per run. Every `sudo` is started directly
  by the Worker process, never by a helper or a shell: with no
  terminal, `sudo` keys its remembered password to the process that
  started it (`man sudoers`, `timestamp_type`, default `tty`, which
  falls back to the parent process). So the Worker keeps the password
  fresh by running `sudo -n -v` on its own timer. Reading a root
  command's output through a pipe is fine. The password box says in
  plain words whose password `sudo` wants. That is root's on Tumbleweed
  (`Defaults targetpw` in `/usr/etc/sudoers`, read 2026-10-02), and the
  user's own on Leap 16 (GRND-0013). The Worker passes `sudo -p %p`,
  so the box receives only that name. With a display, `sudo -A` shows the app's own password
  box; without one, `sudo` asks on the terminal.
- **Logging** goes to one file per run under the user's state directory,
  and the Worker's output is mirrored there.
- **Persistence:** none, beyond the logs and the window's own settings.
  Every run starts from fresh checks.
- **Text** is plain words, written for someone new to openSUSE, and
  every string a user sees is translatable at its source, items'
  titles, sentences and details included. The language follows the
  system's, and the first page offers a choice. A translation no native
  speaker has checked yet ships marked as a draft: the first page's
  choice says so, and so does that page when the draft is the language
  in use. English and checked translations carry no mark. A language
  whose script no installed font covers is named in English in the
  choice and cannot be chosen, and Groundwork starts in English rather
  than in it. A right-to-left language mirrors the layout. Commands a check reads still run with
  `LC_ALL=C`, whatever the user's language. The window follows the
  system's font size and colour scheme, and works at large font sizes.
  Where the desktop is dark but Qt's own colours are light, it opens
  in its Dark theme. The first page offers other
  themes, high contrast among them, for this run only.
- **Tests never touch the real system.** Items name every command
  bare (`zypper`, never `/usr/bin/zypper`), so fakes placed first on
  `PATH` replace them. Core's file reader and every state path take a
  root directory that tests redirect.

## Where each sign is delivered

| Sign | Delivered by |
|---|---|
| S1 — a set-up machine changes nothing | Items' checks, run by Core; the Worker's re-check before apply |
| S2 — one button and one password on a fresh system, video plays | The whole: the Essentials items, the Worker, the Wizard, the AppImage |
| S3 — each row says what it would do, in the user's language | Each item's translatable sentence; the translations; the Wizard's rows |
| S4 — levels in order, any item switched on or off, only those run | The Wizard's pages and toggles; the Worker runs only the items it is given |

## The stack, and what it rules out

- **C++20 and Qt 6 (Core and Widgets), built with CMake, tested with Qt
  Test under CTest, translated with Qt's Linguist tools** — the user's
  choice, and Qt has a wizard component
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
| 4 | 2026-10-02 | 2, each holding every question | 0 | 2 | 5 | — | New run, armed by the translation amendment (7a963e1). 7 verified, 7 fixed: the Worker takes the language on its command line and translates its own text (in the change); and, outside it, every mode refuses an unsupported system; a dependency skipped for any reason but "already done" skips its dependants; switching a dependency off switches off what needs it; the Worker runs items in catalogue order; every `sudo`, keep-alive included, is started by the Worker itself (`man sudoers`: `timestamp_type` default `tty`, falling back to the parent process; read 2026-10-02), and a pipe from the Worker's own child is allowed. The out-of-change findings were fixed here rather than filed, because the next item built under the design needs them and filing would start another run on the same text. Dismissed: FUSE on a fresh install, already ADR-0002's requirement. |
| 5 | 2026-10-02 | 2, each holding every question | 0 | 2 | 3 | — | 5 verified, 5 fixed: the update item can be switched on alone, keeping S4 (both lanes); items name commands bare so fakes on `PATH` replace them (both lanes); the update check reports a skipped repository without blocking every install; source paths are under `src/`, tests and packaging at the top level; the Wizard's process stays until the Worker exits, so neither Qt nor the AppImage mount ends it early. The last three were settled from the lanes' open questions. Unrunnable here: the AppImage mount behaviour, for GRND-0012 to confirm. |
| 6 | 2026-10-02 | 2, each holding every question | 0 | 1 | 1 | — | 2 verified, 2 fixed: the zypper rule's 106-in-a-check now names the update check's exception (both lanes; loop 5's own fix had not been carried into it); a dependency "not needed here" no longer skips its dependants (from a lane's open question). This run's cap (loops 4–6): these fixes are read by no lane; implementation is their reader. Own-fix share of the final loop: 1 of 2, an unpropagated consequence of loop 5's exception rather than a repair of a repair, so a calm cap. Second share: the run was armed by 7a963e1's translation amendment, and 1 of the run's 14 findings lay inside it. Open, for GRND-0007: whether Leap 16 also asks for root's password (`targetpw` read on Tumbleweed only). |
