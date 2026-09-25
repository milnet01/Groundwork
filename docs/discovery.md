# Groundwork — Discovery

> **Purpose — so that later, anyone can tell whether the thing being
> built is still the thing that was wanted.**

This is what everything is checked against for the life of the project.
Design does not start until it is agreed — `~/.claude/workflow.md` § 2.

**Status:** draft, 2026-09-25 — awaiting the user's agreement.

Source material: `docs/brief.md`, including its "Decided at kickoff"
section.

## The problem

A fresh openSUSE install can't play an MP4, doesn't know about Flathub,
has no proprietary graphics driver, and renders fonts poorly. Fixing each
of those means finding a forum post, pasting commands you don't fully
understand, and guessing at the vendor-change prompt in the middle. Some
of that advice is out of date. None of it checks whether a step is
already done.

Groundwork is that set of first steps: one window, each task with a
toggle, a plain-English explanation and a status. Every task checks
itself first, so re-running it is safe.

## Who it is for

- **The author, setting up a fresh install.** Someone who reinstalls or
  sets up new machines and wants the first steps done in one pass.
- **A person new to openSUSE.** Someone who has just installed it,
  doesn't know what "Packman" is, and would otherwise copy commands from
  blog posts.
- **A person re-checking a machine.** Someone who runs it now and then to
  ask "is this machine still set up right?" A secondary use: it does not
  drive the design.

## Signs it is working

Ids are never reused and never renumbered. The roadmap cites these
labels.

- **S1** — On a machine that is already set up, every row reports
  "already done", and nothing is changed.
- **S2** — On a fresh Tumbleweed or Leap virtual machine, one button and
  one password prompt complete every switched-on task, and a video file
  plays afterwards.
- **S3** — Before anything is applied, each row shows in plain English
  what it would do.

## What it deliberately does not do

- **Not an installer.** The system is already installed.
- **Not a settings app.** A short, curated list, not everything YaST can
  do.
- **Not other distributions.** openSUSE only: Tumbleweed and Leap.
- **No dotfile or app-preference management.**
- **Not a tab inside OneUp.** A separate app that reuses OneUp's engine
  patterns.
- **Never asks which openSUSE it is on.** It detects Tumbleweed or Leap
  itself.
