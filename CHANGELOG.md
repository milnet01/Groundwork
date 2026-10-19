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

- **Carrying a setup over: Groundwork lists what is set up on this
  computer, lets you keep or drop each part, and saves it to one file,
  a USB stick being the usual place.** The file holds Groundwork's items
  that are done, the programs you asked for, the software sources you
  added, and your settings app by app. Settings that hold saved
  passwords or keys start switched off, and switching one on says the
  file will then hold them. Starting a new install from the file is not
  built yet (GRND-0079). (GRND-0078)

## [0.4.0] - 2026-10-16

**Theme:** A drive ready for openSUSE.

### Added

- **Preparing a drive for installing openSUSE: Groundwork lays out a
  whole drive, or the free space beside Windows, then lists what to
  choose for each partition in the installer.** A drive in use cannot
  be chosen, and nothing is written until "I understand" is switched
  on. (GRND-0076)
- **Two developer switches: "Tools for Claude Code" installs ripgrep,
  Git and code checkers, and "tree-sitter" installs tree-sitter with
  grammars for common languages.** Tumbleweed and Slowroll get more
  grammars than Leap, which lacks some. Switching on Ants Terminal
  switches tree-sitter off, since Ants Terminal includes it, and the
  row says why. (GRND-0081)
- The first page and the README say that Groundwork is an independent
  project, not an official openSUSE or SUSE product, and not endorsed
  by the openSUSE Project or SUSE.

### Changed

- On a live system, started from a USB stick or a disc, Groundwork no
  longer checks or sets anything up, since nothing there lasts a
  restart; its first page says so (GRND-0077).

## [0.3.1] - 2026-10-15

**Theme:** A machine that keeps running.

### Added

- **Groundwork can now be installed as an openSUSE package, from its own repository for Tumbleweed, Slowroll and Leap 16.0.**

### Fixed

- **The message shown on an older Leap now names Slowroll too, beside Leap 16 and Tumbleweed.**

## [0.3.0] - 2026-10-13

**Theme:** A machine that keeps running.

### Added

- **OneUp: the author's one-window update app, offered beside openSUSE's own updates** (GRND-0074)
  On Tumbleweed and Leap 16.0, where its repository builds it.

- **Video acceleration on AMD graphics: Packman's video driver, so every common video format plays on the graphics card** (GRND-0062)
  Offered where AMD graphics is found.

- **Virtual machines: installs Virtual Machine Manager, on computers whose processor can run them** (GRND-0064)
  openSUSE's kvm_tools pattern: QEMU/KVM, libvirt and Virtual Machine
  Manager. It is ready after a restart.

- **Game controllers and GameMode** (GRND-0063)
  Steam can reach game controllers and VR headsets, and GameMode
  tunes the computer while a game runs. Where SELinux is on, its
  gaming policy stops it blocking Windows games.

- **Ants Terminal, set as the terminal** (GRND-0061)
  Installs Ants Terminal from its own software source, on Tumbleweed
  and Leap 16.0. On KDE Plasma it also becomes the terminal the
  desktop opens.

- **NumLock on at login** (GRND-0060)
  The number pad works straight away at the login screen. Offered
  where the login screen is SDDM; it takes effect the next time that
  screen appears.

- **Less eager swapping** (GRND-0059)
  Programs stay in memory longer before the computer moves them to
  disk (vm.swappiness 10, from the kernel's 60). A lower setting
  already in place is left alone.

- **The freeze recorder** (GRND-0058)
  A small protected service notes what is using memory whenever it
  runs short, in the journal and in /var/log/freeze-recorder.log, so
  the cause of a freeze can be found afterwards. The log drops its
  oldest lines first, so it stays small.

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

- **Media codecs now come from Packman's full repository, and the system is switched to Packman's builds** (GRND-0071)
  As opi does. A system set up with Packman Essentials alone reads
  "not done" until it is switched. ADR-0004 replaces ADR-0003.

- **Each item's choice is an on/off switch instead of a tick box.** (GRND-0069)
  The switch fills with the colour theme's accent when on, grows with the
  font, works from the keyboard, and screen readers still hear a checkable
  control named for the item.

### Fixed

- **The window stays on screen on Wayland, so Back, Next and Apply can be clicked** (GRND-0075)
  On a fresh install the window grew past the bottom of the screen
  from the second page on, hiding its buttons.

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
