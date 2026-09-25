# Groundwork

**A fresh openSUSE install, finished — in one window, in about ten minutes.**

> **Status: not started.** This file is a *brief*, not documentation of
> something that exists. It's here so that when you sit down to build this,
> you don't have to re-think the problem from scratch.
>
> Working name only — rename freely when you start.

---

## The problem

OneUp keeps a machine up to date. Nothing gets a machine to the starting line.

A brand-new openSUSE install is a perfectly good system that can't play an
MP4, doesn't know about Flathub, has no proprietary graphics driver, and
renders fonts in a way that makes people think Linux looks bad. Fixing that
means finding the right forum post for each item, pasting commands you don't
fully understand, and hoping the vendor-change prompt in the middle was the
right answer.

Everyone who installs openSUSE does the same twelve things. **Groundwork is
those twelve things, with a toggle and a plain-English explanation next to
each one.**

## Why not just use an existing tool?

- **`opi`** (the openSUSE Package Installer) is excellent and does part of
  this, but it's a command-line tool that installs one thing at a time. It
  assumes you already know that "Packman codecs" is the thing you want.
- **YaST** configures the system, but assumes you know what you're looking for
  and has no opinion about what a desktop user actually needs.
- **The rest is blog posts.** "10 things to do after installing openSUSE
  Tumbleweed" articles, of varying accuracy and age, that people copy-paste
  from. Some of that advice is now wrong. None of it checks whether you've
  already done step 4.

The gap isn't capability — every individual command exists. The gap is a
curated, explained, **safe-to-re-run** pass over all of it, in a window.

## What it does (v1)

Same shape as OneUp: a list of tasks, each with a toggle, a one-line
explanation, and a status. Run them all with one button and one password.

Candidate tasks — pin the final list against current openSUSE docs when you
start, because this advice ages:

| Task | What it means for the user |
|---|---|
| **Media codecs** | Adds the Packman repository and does the vendor switch properly, so video and audio just play. The single most common reason a new install "doesn't work". |
| **Flatpak + Flathub** | Turns on the app store most modern Linux apps ship through. |
| **Graphics driver** | Detects the card and offers the right driver (NVIDIA repo, or confirms AMD/Intel need nothing). |
| **Fonts & rendering** | Microsoft-compatible fonts and sane rendering defaults, so websites and documents look right. |
| **Firmware updates** | One `fwupdmgr` pass — same code OneUp already has. |
| **Snapshots sanity-check** | Confirms Btrfs snapshots are on and configured, so a bad update is recoverable. |
| **Firewall & SSH** | Confirms sensible defaults rather than changing them silently. |
| **Handy extras** | A short, opinionated package list (`htop`, `p7zip`, `git`, …) with every item visible and toggleable. |

Then: **hand off to OneUp** for the first full update, if it's installed.

### Deliberately not in v1

- **Not an installer.** The system is already installed; this is the first
  ten minutes after.
- **Not "everything YaST can do".** A curated list, not a settings app.
- **Not other distros.** openSUSE-specific, like OneUp. Fedora/Arch have
  their own conventions and their own equivalents of this problem.
- **No dotfile or app-preference management.** That's a different tool.

## How it's built

**Steal OneUp's architecture wholesale** — it exists precisely because this
class of app has a nasty privilege problem, and you've already solved it:

- **`groundwork.sh`** — the engine. All root work lives here. Authenticates
  once via `sudo -A` with `ksshaskpass`, keeps the credential warm, emits
  `@@MARKER@@|payload` lines. Fully usable on its own in a terminal, which
  matters — someone with a broken desktop can still run it.
- **A PySide6 window** that never runs as root, shells out via `QProcess`,
  and parses the markers into rows, progress bars and banners.

Read `OneUp/CLAUDE.md` before writing a line. The hard-won lessons there apply
verbatim and will bite you again otherwise:

- **Never put a privileged call inside a subshell** — no-tty sudo keys its
  cached credential to the parent process id, so `x=$(sudo …)` costs another
  password popup. Use the `sudo_capture` pattern.
- **Nothing you spawn may outlive the engine** — the keep-alive watches the
  engine's pid.
- **Use `tee -a -p`** so a closed GUI doesn't SIGPIPE a transaction to death.
- **Never interrupt a transaction.** Stopping is cooperative, at safe boundaries.

Honestly, the fastest path may be to **fork OneUp's engine** and replace the
step list, rather than writing a second one from scratch.

### The one hard design constraint

**Every task must be idempotent and must detect its own completion.** This is
the difference between a real tool and a blog post in a window. Running
Groundwork on a machine that's already set up must be *safe and boring* — each
row reports "already done" and does nothing.

That means every task needs two halves: a **check** (read-only, no root where
possible) and an **apply**. The window shows the check results first, so you
see what it *would* do before agreeing to anything. It also makes the app
useful as a periodic audit — "is this machine still set up right?"

## Build order

1. **The check pass** — every task's read-only detector, engine only, printed
   in a terminal. *Verify: run on this (fully set-up) machine — every row says
   "already done". That's the whole correctness proof in one command.*
2. **Two apply tasks** — Flatpak/Flathub and the extras list. Lowest risk,
   no vendor changes. *Verify: run in a throwaway VM, then re-run — second run
   reports "already done" and changes nothing.*
3. **The window** — reuse OneUp's marker parser and row widgets. *Verify: check
   results appear as rows with toggles.*
4. **Codecs and the vendor switch** — the risky one; the reason people end up
   in a terminal. *Verify: VM test that video plays afterwards, and that a
   re-run is a no-op.*
5. **Graphics driver detection** — offer, don't force. *Verify: correct
   detection on an AMD machine (this one) and an NVIDIA VM/machine.*
6. **Packaging** — AppImage + RPM + OBS, same as OneUp. Worth doing properly;
   this one is meant to be handed to other people.

## Open questions to settle at kickoff

- **Separate app, or a mode inside OneUp?** A "First run" tab in OneUp would
  reuse everything and be one fewer thing to maintain. But it muddies OneUp's
  clean story ("one click, everything up to date"), and the audiences differ —
  Groundwork is a thing you run once and then forget. *Leaning: separate app,
  shared engine patterns. Decide before writing code, it's expensive later.*
- **Leap as well as Tumbleweed?** OneUp covers both. Repo URLs and some advice
  differ. *Leaning: yes, but detect and adapt, don't ask the user.*
- **How opinionated should the extras list be?** Every item you add is a
  small argument with someone. *Leaning: short list, everything visible,
  nothing on by default that isn't near-universal.*

## Prior art worth a look before starting

- `OneUp/update_system.sh` — the engine to fork; the marker protocol, sudo
  handling, and step-failure semantics are all directly reusable.
- `OneUp/CLAUDE.md` — the conventions section is a list of bugs you get to
  skip. Read it first.
- openSUSE's own documentation for codecs and Packman, at the time you start.
  **Do not trust remembered command lines here** — vendor-change handling in
  particular has changed over the years, and getting it wrong breaks systems.

## When you start

Point `/start-app` at this folder. The task table above is close to a roadmap
already — one item per task, each needing a check and an apply.
