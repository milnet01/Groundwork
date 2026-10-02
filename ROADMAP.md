<!-- ants-roadmap-format: 1 -->
<!-- Generated from the Ants Terminal roadmap store. Edit it with roadmap_log; hand edits are discarded by the next write. -->
# Groundwork — Roadmap

> What is planned, in progress and shipped. [CHANGELOG.md](CHANGELOG.md)
> is the user-facing record of what shipped; released items stay here and
> flip to ✅.
>
> **Format:** `~/.claude/standards/roadmap-format.md`. Theme emojis
> (§ 3.4), priority bands (§ 3.5.2) and the full bullet field set (§ 3.5)
> are defined there and deliberately not restated here.

**Legend**

- ✅ Done · 🚧 In progress · 📋 Planned · 💭 Considered
- 🚫 Dropped (closed, not done)

## 0.1.0 — Essentials

> **Name the version this work ships in, and its theme** — § 3.2 makes a
> release block the default at every version, pre-1.0 included, so the
> roadmap answers what is still needed for the next release. Add
> `(target: YYYY-MM)` once there is a date worth stating.
>
> A phase block (`## P01 — …`) is the alternative where you cannot yet
> place items in a release, and it costs rotation: § 3.9's phase occasion
> is specified and unperformable today.
>
> **Nothing goes here until design is agreed.** Items are broken out of
> the design, and the gate on doing so is that every sign of success in
> `docs/discovery.md` — each carrying an `S<n>` id — is claimed by at
> least one item, and every item
> names what must close before it can start, in `Blocked-by:`
> (`~/.claude/workflow.md` § 5, `roadmap-format.md` § 3.5).

- ✅ [GRND-0001] **Build skeleton: CMake, Qt 6, Qt Test, wired into the CI gate.**
  Serves S2 (the whole). C++20, Qt 6 Core and Widgets, CMake,
  Qt Test under CTest (design: The stack). Fill scripts/local-ci.sh's
  build and test legs; ci.yml installs the toolchain and calls the
  script, adding no step of its own.
  Overlaps every later item on CMakeLists.txt.
  Shipped 2026-10-02 in a00acec: local gate and GitHub run 37020573478
  both green, 2 tests.
  **Layman:** Sets up the empty program so it builds and its tests run on every push.
  Kind: chore.
  Source: design-2026-10-02.
  Lanes: build.

- 📋 [GRND-0002] **Core: read the system's identity and refuse unsupported systems.**
  Serves S2. Reads ID and VERSION_ID from /etc/os-release, never NAME
  or ID_LIKE (design: Which systems it runs on). Supported:
  opensuse-tumbleweed, opensuse-slowroll, opensuse-leap 16 or later.
  IDs from docs/research/2026-10-02-sources.md.
  **Layman:** The app works out which openSUSE it is on, and says plainly if it can't help.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: core.
  Blocked-by: GRND-0001.

- ✅ [GRND-0003] **Core: item interface, command runner, file reader and check runner.**
  Serves S1. Items name read-only commands and files; Core runs them
  with a time limit and reads files through a reader whose root
  directory tests redirect (design: What may depend on what; What
  every part does the same way). Four check results: done, not done,
  not needed here, couldn't tell. Fake commands first on PATH in tests.
  Shipped 2026-10-02 in 12300f7: local gate and GitHub run 37021064015
  both green, 5 tests.
  **Layman:** The shared machinery every setup item uses to look at the system without changing it.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: core.
  Blocked-by: GRND-0001.

- 📋 [GRND-0004] **Core: default selection and dependency closure.**
  Serves S4 and S1. Only Essentials not done start on; the system
  update is switched on only as a dependency of an installing item
  (design: The levels). One rule, used by both the Wizard and the
  Worker.
  **Layman:** Decides which items start ticked, and ticks anything an item needs first.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: core.
  Blocked-by: GRND-0003.

- 📋 [GRND-0005] **Check mode: print every item's state in a terminal.**
  Serves S1: run on this set-up machine, nothing starts switched on
  and every item it was set up with reports already done.
  Entry prints Core's results for the catalogue's checks.
  **Layman:** A command that shows what is and isn't set up, without changing anything.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: entry.
  Blocked-by: GRND-0002, GRND-0003.

- 📋 [GRND-0006] **Worker: root once, run steps, markers, stop, log.**
  Serves S2 and S1. sudo -A with a display, plain sudo on a terminal;
  keep-alive as OneUp's engine does it. Re-checks each item before
  applying, applies only on not done. zypper exit rule: 0, 100-103,
  106 succeed; 103 re-runs; 106 told to the user. Stops only between
  items, survives a closed output. Creates the Worker interface
  reference file: markers, command line, stop-file location.
  Borrows OneUp's lessons, not its code (ADR-0001).
  **Layman:** The part that does the admin work, asks for the password once, and reports progress.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: worker.
  Blocked-by: GRND-0003.

- 📋 [GRND-0007] **Askpass mode: the app's own password box.**
  Serves S2 (one password prompt). Selected by an environment variable
  the Worker sets; sudo passes the prompt as the first argument
  (measured 2026-10-02). Shows sudo's own prompt, which names whose
  password it wants (root's, on Tumbleweed).
  **Layman:** A simple password window, so the app works on any desktop, not only KDE.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: gui, entry.
  Blocked-by: GRND-0006.

- 📋 [GRND-0008] **Wizard: level pages, rows, review page and run page.**
  Serves S3 and S4. Rows show each item's state and its plain-English
  sentence; toggles; dependencies switched on with the reason shown.
  Apply is unavailable with nothing switched on. Closing the window
  asks the Worker to stop. Follows the system font size and colours
  and works at large sizes; refuses to start as root.
  **Layman:** The step-by-step window: one page per level, a summary, then the progress page.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: gui.
  Blocked-by: GRND-0004, GRND-0006, GRND-0032.

- 📋 [GRND-0009] **Item: bring the system up to date.**
  Serves S2. zypper dup on Tumbleweed and Slowroll, zypper update on
  Leap; take the commands from current openSUSE docs when built.
  A dependency of every installing item; S1 does not count it.
  **Layman:** Updates the system before installing anything, so new software fits.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- 📋 [GRND-0010] **Item: Flatpak and Flathub.**
  Serves S2 and S1. Commands from https://flathub.org/setup/openSUSE.
  Rootless check: flatpak remotes.
  **Layman:** Turns on the app store most modern Linux apps ship through.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- 📋 [GRND-0011] **Item: media codecs from Packman Essentials, including browser video.**
  Serves S2 (a video plays). ADR-0003: the Essentials repository, per
  the wiki; a system with the full Packman repository counts as done.
  Slowroll has its own Packman tree. Browser H.264 needs openh264 from
  codecs.opensuse.org (docs/research/2026-10-02-sources.md). Take
  commands from the live wiki when built.
  **Layman:** Makes videos and music play, including in the web browser.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- 📋 [GRND-0012] **AppImage build on the oldest supported system.**
  Serves S2. ADR-0002: runs on a fresh install with nothing installed
  first, so check whether the runtime needs FUSE or libfuse2 on a
  fresh Tumbleweed and Leap 16. Built on Leap 16, the oldest supported
  system (design: The stack).
  **Layman:** Packs the app into one file you download and double-click.
  Kind: package.
  Source: design-2026-10-02.
  Lanes: packaging.
  Blocked-by: GRND-0007, GRND-0008.

- 📋 [GRND-0013] **Release check: S1 to S4 on fresh Tumbleweed and Leap 16 machines.**
  Serves S1, S2, S3 and S4. On a fresh virtual machine of each: one
  button and one password complete the switched-on items and a video
  plays (S2); a second run starts nothing and changes nothing (S1);
  rows say what they would do (S3); levels in order, toggles obeyed
  (S4).
  **Layman:** Tries the finished app on brand-new test machines to prove it works.
  Kind: test.
  Source: design-2026-10-02.
  Lanes: tests.
  Blocked-by: GRND-0005, GRND-0008, GRND-0009, GRND-0010, GRND-0011, GRND-0012.

- 📋 [GRND-0032] **Translation machinery: translatable strings, language choice, right-to-left layout.**
  Serves S3 and S4. Qt Linguist tools in CMake (translation sources
  under translations/); load the translation matching the system
  language, with a choice on the wizard's first page; right-to-left
  languages mirror the layout (design: What every part does the same
  way, Text). A pseudo-translation test proves every visible string is
  translatable. Qt's Linguist tools are not installed on the author's
  machine; installing them needs the root password.
  **Layman:** Lets the app show its words in other languages, including ones written right to left.
  Kind: implement.
  Source: user-request-2026-10-02.
  Lanes: build, core, gui.
  Blocked-by: GRND-0001.

## 0.2.0 — Hardware support

Essentials that depend on the machine's hardware: offered only where the
hardware is found and lacks support.

- 📋 [GRND-0014] **Item: NVIDIA driver, offered only where an NVIDIA card is found.**
  Serves S2 and S3. Detect the card from /sys/bus/pci/devices without
  root. Driver generation by card (G06, G07 for Turing and newer) and
  the NVIDIA repository from SDB:NVIDIA_drivers and Stefan Dirsch's
  notes (docs/research/2026-10-02-sources.md). Leap 16.0 may already
  have it: the check must say so. With Secure Boot, the row says a key
  must be approved at the next restart. Other cards: not needed here.
  **Layman:** Installs the right NVIDIA graphics driver, and warns about the extra step at the next restart.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- 📋 [GRND-0015] **Item: sound firmware, offered only where sound hardware needs it.**
  Serves S2. sof-firmware, per the openSUSE forum tips thread and
  SDB:Audio_troubleshooting (docs/research/2026-10-02-sources.md).
  The check must tell needed from not needed without root.
  **Layman:** Fixes the common 'no sound on my laptop' problem after a new install.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- 📋 [GRND-0016] **Item: Broadcom Wi-Fi, offered only where a Broadcom card is found.**
  Serves S2. broadcom-wl comes from Packman; find out whether the
  Essentials repository carries it, since ADR-0003 adds only that.
  If it does not, the item says so rather than adding the full
  repository silently. Known to break after kernel updates
  (docs/research/2026-10-02-sources.md).
  **Layman:** Gets Broadcom Wi-Fi cards working.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

## 0.3.0 — System setup and configuration

The design's second and third levels.

- 📋 [GRND-0017] **Item: Btrfs snapshots are on.**
  Serves S1 and S3. snapper list-configs works without root (measured
  2026-10-02).
  **Layman:** Confirms the system can be rolled back if an update goes wrong.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- 📋 [GRND-0018] **Item: the firewall is on.**
  Serves S1 and S3. Check with systemctl is-active firewalld;
  firewall-cmd --state is not a reliable rootless check (measured
  2026-10-02).
  **Layman:** Confirms the firewall is switched on, and offers to switch it on if not.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- 📋 [GRND-0019] **Item: firmware updates.**
  Serves S1 and S3. fwupdmgr get-updates runs without root
  (docs/research/2026-10-02-sources.md); whether refresh needs
  authentication is unverified.
  **Layman:** Checks for and installs firmware updates for the computer's hardware.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- 📋 [GRND-0020] **Item: laptop power settings, offered only where there is a battery.**
  Serves S3. TLP conflicts with power-profiles-daemon on openSUSE
  (https://linrunner.de/tlp/installation/opensuse.html); pick one
  and say why.
  **Layman:** Sets up battery-friendly power settings on laptops.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- 📋 [GRND-0021] **Item: the computer's name.**
  Serves S3 and S4. The first item that takes a value from the user,
  not only a toggle; the Wizard row needs a text field.
  **Layman:** Lets you give the computer a name of your choice.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items, gui.
  Blocked-by: GRND-0003, GRND-0008.

- 📋 [GRND-0022] **Item: remote login over SSH, off by default.**
  Serves S3. Leap 16.0 turns password root login over SSH off on new
  installs (release notes 3.5); keep that default.
  **Layman:** Lets you switch remote login to this computer on or off.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- 📋 [GRND-0023] **Item: the clock setting, offered only where Windows is also installed.**
  Serves S3. https://itsfoss.com/wrong-time-dual-boot/ describes the
  problem; the check must find a Windows install without root.
  **Layman:** Stops the clock being wrong after switching between Linux and Windows.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- 📋 [GRND-0024] **Final page: offer to open OneUp when it is installed.**
  Serves S2 (the run ends cleanly). Design: The levels.
  **Layman:** After setup, points you to OneUp to keep the computer up to date.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: gui.
  Blocked-by: GRND-0008.

## 0.4.0 — Nice to have

The design's fourth level: extras, each its own toggle.

- 📋 [GRND-0025] **Item group: everyday apps.**
  Serves S3 and S4. Each app its own toggle. Choose Flathub or the
  vendor's repository per app and say why in the item; opi's targets
  list the common asks (https://github.com/openSUSE/opi).
  **Layman:** Offers popular apps such as other web browsers, chat and office programs.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- 📋 [GRND-0026] **Item group: gaming.**
  Serves S3 and S4.
  **Layman:** Offers Steam and other gaming tools.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- 📋 [GRND-0027] **Item group: developer tools.**
  Serves S3 and S4.
  **Layman:** Offers common programming tools.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- 📋 [GRND-0028] **Item: a backup tool for the user's own files.**
  Serves S3 and S4.
  **Layman:** Offers a tool to back up your documents and photos, which system snapshots do not cover.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- 📋 [GRND-0029] **Item: Microsoft-compatible fonts.**
  Serves S3 and S4. fetchmsttfonts is in Tumbleweed and was not found
  in Leap 16.0's main repository: not needed here there, unless
  another source is found.
  **Layman:** Installs fonts like Arial and Times New Roman so documents and websites look right.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- 📋 [GRND-0030] **Item group: handy command-line programs.**
  Serves S3 and S4.
  **Layman:** Offers a short list of useful small programs, each with its own switch.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

## 0.5.0 — Languages

Translations, asked for by the user on 2026-10-02: Asian and right-to-left
languages, and Afrikaans. The machinery ships in 0.1.0; each item here is one
translation.

- 📋 [GRND-0033] **Translation: Afrikaans.**
  Serves S3. The user is South African and can check this one.
  **Layman:** The whole app in Afrikaans.
  Kind: implement.
  Source: user-request-2026-10-02.
  Lanes: translations.
  Blocked-by: GRND-0032, GRND-0008.

- 📋 [GRND-0034] **Translations: Arabic and Hebrew, right to left.**
  Serves S3 and S4. Proves the mirrored layout on every page.
  **Layman:** The app in Arabic and Hebrew, laid out right to left.
  Kind: implement.
  Source: user-request-2026-10-02.
  Lanes: translations, gui.
  Blocked-by: GRND-0032, GRND-0008.

- 📋 [GRND-0035] **Translations: Chinese (Simplified), Japanese and Korean.**
  Serves S3. Check that a fresh install shows these scripts; if a font
  is missing, the app says so rather than showing empty boxes.
  **Layman:** The app in Chinese, Japanese and Korean.
  Kind: implement.
  Source: user-request-2026-10-02.
  Lanes: translations.
  Blocked-by: GRND-0032, GRND-0008.

- 📋 [GRND-0036] **Translation: Hindi.**
  Serves S3. Same font check as the East Asian item.
  **Layman:** The app in Hindi.
  Kind: implement.
  Source: user-request-2026-10-02.
  Lanes: translations.
  Blocked-by: GRND-0032, GRND-0008.

- 📋 [GRND-0037] **Translations: German, French, Spanish and Portuguese.**
  Serves S3.
  **Layman:** The app in four widely used European languages.
  Kind: implement.
  Source: user-request-2026-10-02.
  Lanes: translations.
  Blocked-by: GRND-0032, GRND-0008.

## Backlog — no version yet

Items not yet placed in a release. Each moves into a version section once it is
scheduled.

- 📋 [GRND-0031] **RPM and OBS packaging.**
  Serves S2. After 0.1.0 (ADR-0002 keeps only the AppImage in 0.1.0).
  **Layman:** Makes the app installable from openSUSE's own software sources.
  Kind: package.
  Source: brief-2026-09-25.
  Lanes: packaging.
  Blocked-by: GRND-0012.
