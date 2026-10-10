# Changelog

All notable changes to Groundwork are documented in this file.

The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). The format
contract is `~/.claude/standards/changelog-format.md` § 4.

The `[Unreleased]` block stays at the top, always, even when empty.

## [Unreleased]

### Added

- **A shorter wait at the boot menu** (GRND-0057)
  The boot menu shows for 3 seconds, so the computer starts sooner,
  with time still to choose from it. Works with systemd-boot,
  grub2-bls and grub2. A menu that already waits 3 seconds or less,
  or is hidden, is left alone.

- **A 1 GB limit on the system log** (GRND-0056)
  The system's log keeps at most 1 GB and deletes its oldest entries
  first, so it can no longer fill a small drive. A limit of 1 GB or
  less that is already set is left alone.

- **Calmer wake from hibernation: maintenance jobs missed while asleep are spread over half an hour instead of all starting at once.** (GRND-0055)

- **Smoother spinning hard drives: every spinning drive uses the bfq scheduler, so the desktop stays smooth while one is busy.** (GRND-0054)

- **Emergency keyboard escape: every Alt+SysRq key on, so a frozen computer can be restarted cleanly.** (GRND-0053)
  A frozen computer can be restarted safely from the keyboard instead of the power button.

- **RAM protection: when memory runs short, the most memory-hungry app is closed before the desktop freezes.** (GRND-0052)
  When memory runs out, the desktop stays usable and the app using the most memory is closed, instead of the whole computer locking up.

- **Colour themes: the window follows the desktop's light or dark, and the first page offers others, high contrast among them.** (GRND-0050)
  The app can be dark, which is easier on light-sensitive eyes; it follows the desktop unless you choose otherwise.

### Changed

- **Each item's choice is an on/off switch instead of a tick box.** (GRND-0069)
  The switch fills with the colour theme's accent when on, grows with the
  font, works from the keyboard, and screen readers still hear a checkable
  control named for the item.

### Fixed

- **The update row no longer says "not done" beside "no updates were waiting".** (GRND-0051)
  It shows what the check found, in the window and in --check.

## [0.1.0] - 2026-10-10

**Theme:** Every level, from essentials to extras.

### Added

- **Languages nobody fluent has checked yet are labelled as drafts.** (GRND-0041)
  The language choice says so, and so does the first page while a draft
  is in use.

- **Translation files for fourteen languages, built into the app.** (GRND-0038)
  Each language is offered once it has been translated.

- **A step-by-step setup window: choose a language, pick items level by level, review, then apply with one password** (GRND-0008)
  Items start ticked only where they are essential and not yet done.
  Ticking an item ticks what it needs, and the row says why.

- **`groundwork --check` lists what is and is not set up, and changes nothing** (GRND-0005)

- **Setup runs as its own process that asks for the password once, re-checks each item first, and stops safely between items** (GRND-0006)

- **Groundwork's own password box, so it works on any desktop** (GRND-0007)

- **Essentials: system update, media codecs from Packman Essentials, Flatpak and Flathub** (GRND-0011)

- **Hardware support where it is needed: NVIDIA driver, laptop sound firmware, Broadcom Wi-Fi** (GRND-0014)

- **System setup and configuration: snapshots, firewall, firmware updates, laptop power, computer name, remote login, dual-boot clock** (GRND-0017)

- **Extras, each its own choice: popular apps from Flathub, gaming, developer tools, a backup tool, command-line programs, Microsoft fonts** (GRND-0025)

- **Language support, including right-to-left layouts** (GRND-0032)

- **A single-file AppImage that runs without installing anything first** (GRND-0012)

- **Items that accept a licence (the NVIDIA driver, Microsoft fonts) never start switched on.** (GRND-0039)
  You switch them on yourself, so nobody accepts a licence by accident.
