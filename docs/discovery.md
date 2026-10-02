# Groundwork — Discovery

> **Purpose — so that later, anyone can tell whether the thing being
> built is still the thing that was wanted.**

This is what everything is checked against for the life of the project.
Design does not start until it is agreed — `~/.claude/workflow.md` § 2.

**Status:** agreed by the user, 2026-10-02. Amended the same day for
levels and configuration, and agreed again. S1, S2 and the supported
systems narrowed the same day to match the design. It is expected to change as
the work goes on. A change is made here, with the user's agreement
(`~/.claude/workflow.md` §§ 4 and 7).

Source material: `docs/brief.md`, including its "Decided at kickoff"
section.

## The problem

A fresh openSUSE install can't play an MP4, doesn't know about Flathub,
has no proprietary graphics driver, and renders fonts poorly. Fixing each
of those means finding a forum post, pasting commands you don't fully
understand, and guessing at the vendor-change prompt in the middle. Some
of that advice is out of date. None of it checks whether a step is
already done.

Groundwork lets anyone set up openSUSE the way they want, in one
window. It offers a list of items, and the user picks the ones they
want. The items are grouped into levels that run in order, from
essentials to nice-to-haves. Some items install things; others
configure the system. Each item has a toggle, a plain-English
explanation and a status. Every item checks itself first, so re-running
it is safe.

## Who it is for

- **The author, or anyone, setting up a fresh install.** Someone who
  reinstalls or sets up new machines and wants each one set up their way
  in one pass.
- **A person new to openSUSE.** Someone who has just installed it,
  doesn't know what "Packman" is, and would otherwise copy commands from
  blog posts.
- **A person re-checking a machine.** Someone who runs it now and then to
  ask "is this machine still set up right?" A secondary use: it does not
  drive the design.

## Signs it is working

Ids are never reused and never renumbered. The roadmap cites these
labels.

- **S1** — On a machine that is already set up, no item starts
  switched on, every item it was set up with reports "already done"
  (the system update aside: it reports whether updates are waiting),
  and a run changes nothing.
- **S2** — On a fresh Tumbleweed or Leap 16 virtual machine, one button
  and one password prompt complete every switched-on task, and a video
  file plays afterwards.
- **S3** — Before anything is applied, each row shows in plain English
  what it would do.
- **S4** — Items are shown in levels, essentials first. Before anything
  runs, the user can switch any item on or off, and only the switched-on
  items run.

## What it deliberately does not do

- **Not an installer.** The system is already installed.
- **Not a replacement for YaST.** It configures the common choices a new
  install needs, each explained. It does not offer every setting.
- **Not other distributions.** openSUSE only: Tumbleweed, Slowroll, and Leap 16
  or later.
- **No dotfile or app-preference management.**
- **Not a tab inside OneUp.** A separate app that reuses OneUp's engine
  patterns.
- **Never asks which openSUSE it is on.** It detects which supported openSUSE
  it is on itself.
