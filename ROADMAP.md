<!-- ants-roadmap-format: 1 -->
<!-- Generated from the Ants Terminal roadmap store. Edit it with roadmap_log; hand edits are discarded by the next write. -->
<!-- ants-roadmap-id-high-water: GRND=81 -->
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

## 0.1.0 — Every level, from essentials to extras

The first release: every level of the design, Essentials to Nice to
have, with the translations as drafts. The user chose on 2026-10-10 to
ship it all as 0.1.0 rather than as the four releases first planned.

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

- ✅ [GRND-0002] **Core: read the system's identity and refuse unsupported systems.**
  Serves S2. Reads ID and VERSION_ID from /etc/os-release, never NAME
  or ID_LIKE (design: Which systems it runs on). Supported:
  opensuse-tumbleweed, opensuse-slowroll, opensuse-leap 16 or later.
  IDs from docs/research/2026-10-02-sources.md.
  Shipped 2026-10-02 in e555d58: local gate and GitHub run 37023262086
  both green. Each mode enforces the refusal in its own item.
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

- ✅ [GRND-0004] **Core: default selection and dependency closure.**
  Serves S4 and S1. Only Essentials not done start on; the system
  update is switched on only as a dependency of an installing item
  (design: The levels). One rule, used by both the Wizard and the
  Worker.
  Shipped 2026-10-02 in 6d973ae: local gate and GitHub run 37023636302
  both green.
  **Layman:** Decides which items start ticked, and ticks anything an item needs first.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: core.
  Blocked-by: GRND-0003.

- ✅ [GRND-0005] **Check mode: print every item's state in a terminal.**
  Serves S1: run on this set-up machine, nothing starts switched on
  and every item it was set up with reports already done.
  Entry prints Core's results for the catalogue's checks.
  Shipped 2026-10-02 in 403776c: local gate and GitHub run 37024140249
  green. On the author's Tumbleweed it printed "Nothing would start
  switched on" (S1, for the items built so far).
  **Layman:** A command that shows what is and isn't set up, without changing anything.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: entry.
  Blocked-by: GRND-0002, GRND-0003.

- ✅ [GRND-0006] **Worker: root once, run steps, markers, stop, log.**
  Serves S2 and S1. sudo -A with a display, plain sudo on a terminal;
  every sudo, keep-alive included, started by the Worker itself (design: Root). Re-checks each item before
  applying, applies only on not done. zypper exit rule: 0, 100-103,
  106 succeed; 103 re-runs; 106 told to the user. Stops only between
  items, survives a closed output. Creates the Worker interface
  reference file: markers, command line, stop-file location.
  Borrows OneUp's lessons, not its code (ADR-0001).
  Shipped 2026-10-02 in dc75532: local gate and GitHub run 37025886274
  green, 12 end-to-end worker cases with fake sudo, zypper and flatpak.
  Real root runs come with GRND-0013. --lang is accepted; loading it
  waits for GRND-0032.
  **Layman:** The part that does the admin work, asks for the password once, and reports progress.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: worker.
  Blocked-by: GRND-0003.

- ✅ [GRND-0007] **Askpass mode: the app's own password box.**
  Serves S2 (one password prompt). Selected by an environment variable
  the Worker sets; sudo passes the prompt as the first argument
  (measured 2026-10-02). Shows sudo's own prompt, which names whose
  password it wants (root's, on Tumbleweed).
  Open (design gate loop 6, 2026-10-02): /usr/etc/sudoers has Defaults
  targetpw on Tumbleweed, so sudo asks for root's password. Check
  whether Leap 16 does the same, and whether a fresh Leap 16 can have no
  root password, which would bear on S2's one password prompt.
  Shipped 2026-10-02 in aa45a71: local gate and GitHub run 37026224446
  green. A real sudo -A prompt is first seen under GRND-0013; the open
  question on Leap 16's targetpw stays there too.
  **Layman:** A simple password window, so the app works on any desktop, not only KDE.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: gui, entry.
  Blocked-by: GRND-0006.

- ✅ [GRND-0008] **Wizard: level pages, rows, review page and run page.**
  Serves S3 and S4. Rows show each item's state and its plain-English
  sentence; toggles; dependencies switched on with the reason shown.
  Apply is unavailable with nothing switched on. Closing the window
  asks the Worker to stop. Follows the system font size and colours
  and works at large sizes; refuses to start as root.
  Shipped 2026-10-02 in 4caedef: local gate and GitHub run 37027912051
  green; pages checked by eye at 18 pt offscreen. Against the real
  Worker and sudo: GRND-0013.
  **Layman:** The step-by-step window: one page per level, a summary, then the progress page.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: gui.
  Blocked-by: GRND-0004, GRND-0006, GRND-0032.

- ✅ [GRND-0009] **Item: bring the system up to date.**
  Serves S2. zypper dup on Tumbleweed and Slowroll, zypper update on
  Leap; take the commands from current openSUSE docs when built.
  A dependency of every installing item; S1 does not count it.
  Shipped 2026-10-02 in f26a71d: local gate and GitHub run 37024548661
  green. The check never says done (a rootless check cannot see a dup);
  the apply first runs for real under GRND-0013.
  **Layman:** Updates the system before installing anything, so new software fits.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0010] **Item: Flatpak and Flathub.**
  Serves S2 and S1. Commands from https://flathub.org/setup/openSUSE.
  Rootless check: flatpak remotes.
  Shipped 2026-10-02 in 2488b45: local gate and GitHub run 37023886515
  green. The check is proven by tests and on the author's machine; the
  apply first runs for real under GRND-0013.
  **Layman:** Turns on the app store most modern Linux apps ship through.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0011] **Item: media codecs from Packman Essentials, including browser video.**
  Serves S2 (a video plays). ADR-0003: the Essentials repository, per
  the wiki; a system with the full Packman repository counts as done.
  Slowroll has its own Packman tree. Browser H.264 needs openh264 from
  codecs.opensuse.org (docs/research/2026-10-02-sources.md). Take
  commands from the live wiki when built.
  Shipped 2026-10-02 in 9866067: local gate and GitHub run 37025068071
  green. Correction: that commit body says the red check ran; it did not
  (the break failed to compile under -Werror, so no test ran). Re-run
  the same day with a compiling break: reusesAnExistingPackmanRepository
  failed, restored green. Commands come from the wiki's 2026-05-11
  snapshot; GRND-0013 checks them against the live page and on fresh
  systems.
  **Layman:** Makes videos and music play, including in the web browser.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0012] **AppImage build on the oldest supported system.**
  Serves S2. ADR-0002: runs on a fresh install with nothing installed
  first, so check whether the runtime needs FUSE or libfuse2 on a
  fresh Tumbleweed and Leap 16. Built on Leap 16, the oldest supported
  system (design: The stack).
  Shipped 2026-10-02 in d98e78f: local gate and GitHub run 37033646998
  green; the AppImage ran --version, --check and its window (on Xvfb) on
  the author's Tumbleweed. Found in a bare Tumbleweed container: it
  needs libGLX.so.0 (package libglvnd), which a bare container lacks;
  whether every fresh desktop install has it, and whether FUSE is
  present, is checked under GRND-0013.
  **Layman:** Packs the app into one file you download and double-click.
  Kind: package.
  Source: design-2026-10-02.
  Lanes: packaging.
  Blocked-by: GRND-0007, GRND-0008.

- ✅ [GRND-0013] **Release check: S1 to S4 on fresh Tumbleweed and Leap 16 machines.**
  Serves S1, S2, S3 and S4. On a fresh virtual machine of each: one
  button and one password complete the switched-on items and a video
  plays (S2); a second run starts nothing and changes nothing (S1);
  rows say what they would do (S3); levels in order, toggles obeyed
  (S4).
  Progress 2026-10-02: tests/realrun/run-in-container.sh ran the
  AppImage's Worker for real in fresh Tumbleweed and Leap 16.0
  containers (a normal user, passwordless sudo). On both: AUTH ok once;
  system-update, media-codecs and flathub ended ok; check mode then
  showed codecs and Flathub already done; ffmpeg lists h264 and hevc
  decoders; libavcodec comes from Packman. Leap's $releasever URL
  resolved. Found: the AppImage needs the desktop's X11, OpenGL and font
  libraries (libxcb, libGLX, libfontconfig and others, which AppImages
  leave to the system); a bare container lacks them. Still owed for S1
  to S4: fresh desktop VMs, with the Wizard, the real password box,
  FUSE, and a video playing.
  Decided 2026-10-07: the user chose two virtual machines (fresh
  Tumbleweed and fresh Leap 16 with a desktop, about 4 GB each), run one
  at a time, with the user looking at the screen for the visual checks.
  Progress 2026-10-08, Tumbleweed (snapshot 20261007, KDE, YaST
  default install, QEMU VM): S1, S2, S3, S4 pass. AppImage built from
  4eebc5f started with no setup. Defaults on: system update, media
  codecs; Flathub, VLC, snapshots, firewall, firmware already done;
  hardware rows greyed "not needed here". Switched on Computer name
  (gw-test) only; Ready-to-apply listed exactly those three. One
  password; all done in about 3 minutes. After: hostname gw-test,
  libavcodec from Packman, h264 and hevc decoders, sshd still disabled,
  no Flathub apps; an H.264 file played in VLC. Second run: nothing
  switched on, codecs and name "already done", update "No updates were
  waiting", Apply greyed out. Found: the AppImage lacks Qt's Wayland
  plugin (runs through XWayland); the password box shows sudo's raw
  "[sudo] password for root:" line.
  Resolved 2026-10-08, Leap 16.0 (Build178.27, KDE, Agama default
  install with the first user as sudo admin, QEMU VM): S1, S2, S3, S4
  pass. Defaults on: system update, media codecs, Flathub ("not added
  yet"); snapshots, firewall, firmware, VLC already done. Unticking the
  update also switched off codecs and Flathub, with a note saying why;
  Apply then said nothing is switched on. With the three on, one
  password (the user's own) finished all three: 391 updates, Packman
  codecs, Flathub. After: libavcodec61 and 62 from Packman, h264 and
  hevc decoders, flathub remote, hostname and sshd untouched; an H.264
  file played in VLC. Second run: nothing switched on, codecs and
  Flathub "already done", Apply greyed out. Both systems pass S1 to S4.
  Findings filed as GRND-0045 to GRND-0049.
  **Layman:** Tries the finished app on brand-new test machines to prove it works.
  Kind: test.
  Source: design-2026-10-02.
  Lanes: tests.
  Blocked-by: GRND-0005, GRND-0008, GRND-0009, GRND-0010, GRND-0011, GRND-0012.

- ✅ [GRND-0032] **Translation machinery: translatable strings, language choice, right-to-left layout.**
  Serves S3 and S4. Qt Linguist tools in CMake (translation sources
  under translations/); load the translation matching the system
  language, with a choice on the wizard's first page; right-to-left
  languages mirror the layout (design: What every part does the same
  way, Text). A pseudo-translation test proves every visible string is
  translatable. Qt's Linguist tools are not installed on the author's
  machine; installing them needs the root password.
  Started 2026-10-02 with the half that needs no Linguist tools:
  language loading and choice, right-to-left direction, and a
  pseudo-translation test. Building .ts/.qm files waits for
  qt6-linguist-devel (Main Repository OSS), which needs the user's root
  password.
  Shipped 2026-10-02 in 7b05c64: local gate and GitHub run 37026680184
  green. Building .ts/.qm files is its own item, GRND-0038, which waits
  for the Linguist tools.
  **Layman:** Lets the app show its words in other languages, including ones written right to left.
  Kind: implement.
  Source: user-request-2026-10-02.
  Lanes: build, core, gui.
  Blocked-by: GRND-0001.

- ✅ [GRND-0043] **README still holds the starter template text.**
  Found 2026-10-08 while answering the website session. README.md's
  one-line summary is the placeholder "One line: what this does, for
  whom." and its Status says "Just scaffolded. Nothing has been built".
  Write the real summary and status before the first release; the
  website page will be built from it.
  Resolved (2026-10-08): README.md now holds the real summary, status
  and usage. Install stays a placeholder until there is a release.
  **Layman:** The project's front page still says nothing has been built, which is no longer true.
  Kind: doc-fix.
  Source: in-session-2026-10-08.
  Lanes: docs.

- ✅ [GRND-0014] **Item: NVIDIA driver, offered only where an NVIDIA card is found.**
  Serves S2 and S3. Detect the card from /sys/bus/pci/devices without
  root. Driver generation by card (G06, G07 for Turing and newer) and
  the NVIDIA repository from SDB:NVIDIA_drivers and Stefan Dirsch's
  notes (docs/research/2026-10-02-sources.md). Leap 16.0 may already
  have it: the check must say so. With Secure Boot, the row says a key
  must be approved at the next restart. Other cards: not needed here.
  Shipped 2026-10-02 in c8cc4c5: local gate and GitHub run 37038762087
  green; package names proven by dry runs in Tumbleweed and Leap 16
  containers. Not run on a real NVIDIA card. Two decisions on the user's
  list: the licence accepted by a pre-ticked item, and Tumbleweed's
  current G07 version skew.
  **Layman:** Installs the right NVIDIA graphics driver, and warns about the extra step at the next restart.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0015] **Item: sound firmware, offered only where sound hardware needs it.**
  Serves S2. sof-firmware, per the openSUSE forum tips thread and
  SDB:Audio_troubleshooting (docs/research/2026-10-02-sources.md).
  The check must tell needed from not needed without root.
  Shipped 2026-10-02 in 5bae708: local gate and GitHub run 37037026258
  green; on the author's machine the check reads not needed here. Apply
  first runs for real on SOF hardware, which this machine is not.
  **Layman:** Fixes the common 'no sound on my laptop' problem after a new install.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0016] **Item: Broadcom Wi-Fi, offered only where a Broadcom card is found.**
  Serves S2. broadcom-wl comes from Packman; find out whether the
  Essentials repository carries it, since ADR-0003 adds only that.
  If it does not, the item says so rather than adding the full
  repository silently. Known to break after kernel updates
  (docs/research/2026-10-02-sources.md).
  Shipped 2026-10-02 in 6db56d0: local gate and GitHub run 37037627999
  green; on the author's machine the check reads not needed here. Apply
  first runs for real on a listed Broadcom chip.
  **Layman:** Gets Broadcom Wi-Fi cards working.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0039] **Licence-bearing items never start switched on.**
  Serves S4. GRND-0014 installs with --auto-agree-with-licenses (zypper
  aborts without it), and as designed an Essentials item that is not done
  starts ticked. Proposal: an item flag that keeps it off by default.
  Needs the user's yes and a one-line design change with its review
  (docs/design.md, The levels). Recommended 2026-10-02; waiting on the
  user's decision.
  Decided 2026-10-07: the user said yes. docs/design.md, The levels,
  amended the same day; the user ruled no review is required.
  Shipped 2026-10-07: Item::acceptsLicence(); Core's default selection
  skips such items and orderProblems() refuses a dependency on one.
  NVIDIA driver and Microsoft fonts set it. Tests in tst_selection,
  tst_nvidiaitem and tst_fontsitem, seen red before the change.
  **Layman:** The NVIDIA driver accepts NVIDIA's licence, so you would tick it yourself rather than find it pre-ticked.
  Kind: ux.
  Source: in-session-2026-10-02.
  Lanes: core, items.

- ✅ [GRND-0017] **Item: Btrfs snapshots are on.**
  Serves S1 and S3. snapper list-configs works without root (measured
  2026-10-02).
  Shipped 2026-10-02 in caa09cf: local gate and GitHub run 37039826131
  green; on the author's machine the check reads already done.
  **Layman:** Confirms the system can be rolled back if an update goes wrong.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0018] **Item: the firewall is on.**
  Serves S1 and S3. Check with systemctl is-active firewalld;
  firewall-cmd --state is not a reliable rootless check (measured
  2026-10-02).
  Shipped 2026-10-02 in 193f5fb: local gate and GitHub run 37035595316
  green; on the author's machine the check reads already done. Apply
  first runs for real under GRND-0013.
  **Layman:** Confirms the firewall is switched on, and offers to switch it on if not.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0019] **Item: firmware updates.**
  Serves S1 and S3. fwupdmgr get-updates runs without root
  (docs/research/2026-10-02-sources.md); whether refresh needs
  authentication is unverified.
  Shipped 2026-10-02 in 88e46b5: local gate and GitHub run 37039223504
  green; on the author's machine the check reads already done. Apply
  first runs for real where firmware updates wait.
  **Layman:** Checks for and installs firmware updates for the computer's hardware.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0020] **Item: laptop power settings, offered only where there is a battery.**
  Serves S3. TLP conflicts with power-profiles-daemon on openSUSE
  (https://linrunner.de/tlp/installation/opensuse.html); pick one
  and say why.
  Shipped 2026-10-02 in dff0338: local gate and GitHub run 37039533482
  green; on the author's desktop the check reads not needed here. Apply
  first runs for real on a laptop.
  **Layman:** Sets up battery-friendly power settings on laptops.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0021] **Item: the computer's name.**
  Serves S3 and S4. The first item that takes a value from the user,
  not only a toggle; the Wizard row needs a text field.
  Shipped 2026-10-02 in 61bdcdb: local gate and GitHub run 37040365830
  green; on the author's machine the check reads already done (AntsPC).
  Values reach the Worker through --set.
  **Layman:** Lets you give the computer a name of your choice.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items, gui.
  Blocked-by: GRND-0003, GRND-0008.

- ✅ [GRND-0022] **Item: remote login over SSH, off by default.**
  Serves S3. Leap 16.0 turns password root login over SSH off on new
  installs (release notes 3.5); keep that default.
  Shipped 2026-10-02 in 193f5fb: local gate and GitHub run 37035595316
  green; on the author's machine the check reads switched off, which is
  true there. Apply first runs for real under GRND-0013.
  **Layman:** Lets you switch remote login to this computer on or off.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0023] **Item: the clock setting, offered only where Windows is also installed.**
  Serves S3. https://itsfoss.com/wrong-time-dual-boot/ describes the
  problem; the check must find a Windows install without root.
  Shipped 2026-10-02 in 77b7dee: local gate and GitHub run 37036031656
  green; on the author's machine the check reads not needed here. The
  Linux-side fix is a user decision on the list.
  **Layman:** Stops the clock being wrong after switching between Linux and Windows.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0024] **Final page: offer to open OneUp when it is installed.**
  Serves S2 (the run ends cleanly). Design: The levels.
  Shipped 2026-10-02 in 9e59e60: local gate and GitHub run 37036466994
  green.
  **Layman:** After setup, points you to OneUp to keep the computer up to date.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: gui.
  Blocked-by: GRND-0008.

- ✅ [GRND-0025] **Item group: everyday apps.**
  Serves S3 and S4. Each app its own toggle. Choose Flathub or the
  vendor's repository per app and say why in the item; opi's targets
  list the common asks (https://github.com/openSUSE/opi).
  Shipped 2026-10-02 in 9679a25: Chrome, Brave, VLC, Discord, Zoom,
  Spotify from Flathub; local gate and GitHub run 37041400283 green. The
  list is on the user's decision list.
  **Layman:** Offers popular apps such as other web browsers, chat and office programs.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0026] **Item group: gaming.**
  Serves S3 and S4.
  Shipped 2026-10-02 in 9679a25: Steam and Bottles from Flathub.
  **Layman:** Offers Steam and other gaming tools.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0027] **Item group: developer tools.**
  Serves S3 and S4.
  Shipped 2026-10-02 in 9679a25: gcc, gcc-c++ and make; VSCodium from
  Flathub.
  **Layman:** Offers common programming tools.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0028] **Item: a backup tool for the user's own files.**
  Serves S3 and S4.
  Shipped 2026-10-02 in 9679a25: Déjà Dup from Flathub.
  **Layman:** Offers a tool to back up your documents and photos, which system snapshots do not cover.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0029] **Item: Microsoft-compatible fonts.**
  Serves S3 and S4. fetchmsttfonts is in Tumbleweed and was not found
  in Leap 16.0's main repository: not needed here there, unless
  another source is found.
  Shipped 2026-10-02 in b7f7668: local gate and GitHub run 37040782797
  green; on the author's machine the check reads not done (no Arial
  there).
  **Layman:** Installs fonts like Arial and Times New Roman so documents and websites look right.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0030] **Item group: handy command-line programs.**
  Serves S3 and S4.
  Shipped 2026-10-02 in 9679a25: htop, 7-Zip, Git, fastfetch.
  **Layman:** Offers a short list of useful small programs, each with its own switch.
  Kind: implement.
  Source: design-2026-10-02.
  Lanes: items.
  Blocked-by: GRND-0003.

- ✅ [GRND-0038] **Build translation files with Qt's Linguist tools, locally and on GitHub.**
  Serves S3. Extract strings into translations/*.ts and compile .qm
  files into the program's resources at :/i18n, which
  core/translations.cpp already reads. Install the tools in ci.yml too.
  qt6-linguist-devel 6.11.2 installed here on 2026-10-07.
  Shipped 2026-10-07: translations/groundwork_<code>.ts for af ar de es
  fr he hi ja ko pt xh zh_CN zu, compiled by lrelease into Core's
  resources at :/i18n; update_translations refreshes them. A language is
  offered only when its .qm is non-empty. Found and fixed: main.cpp,
  systemidentity.cpp, packman.cpp and appitems.cpp strings were extracted
  under the wrong context or none. tst_translations checks the files match
  the source and every looked-up context. Verified: gate 28/28 here;
  Ubuntu 24.04 (Qt 6.4.2) 28/28; Leap 16.0 release build.
  **Layman:** Sets up the tools that turn translated text into files the app can load.
  Kind: implement.
  Source: split-from-GRND-0032-2026-10-02.
  Lanes: build, translations.

- ✅ [GRND-0041] **Unchecked translations are marked as drafts.**
  Serves S3. docs/design.md, Text: a translation no native speaker has
  checked ships marked as a draft, on the first page's language choice
  and on that page when it is the language in use. The user checks
  Afrikaans; the rest ship as drafts (user's decision, 2026-10-07).
  Resolved (2026-10-08): the language choice shows "(draft)" after an
  unchecked language, and the first page carries a notice when one is in
  use. The checked list lives in core/translations.cpp and is empty until
  the user approves Afrikaans.
  **Layman:** Languages nobody fluent has checked yet are labelled as drafts, so people know.
  Kind: implement.
  Source: user-request-2026-10-07.
  Lanes: translations, gui.

- ✅ [GRND-0042] **Bold Chinese, Japanese and Korean text is smeared.**
  Seen 2026-10-08 on screenshots from demoreel (zh_CN, zh_TW, ko worst;
  Japanese kana look clean). Item titles, which the wizard draws bold,
  come out as heavy blobs, e.g. "媒体解码器"; disabled titles such as
  "音频固件" are close to unreadable. Body text is fine.
  The installed fonts are variable fonts (NotoSansCJK*-VF.otf) whose
  Bold instance fontconfig lists, so the likely cause is Qt faking bold
  on fallback glyphs instead of using that instance. Unverified; find
  the cause before choosing a fix.
  Located (2026-10-08): the lead above is wrong. Qt 6.11.2 does use
  the font's real Bold instance, then thickens it again with its own
  fake bold. A probe drawing "Noto Sans CJK SC" at 13 px measured ink
  482/655/773/1205/1311 for Light/Regular/Medium/Bold/Black; with
  QT_NO_SYNTHESIZED_BOLD=1 Bold and Black fall to 919/1052 and look
  crisp, the rest unchanged. Same with the family named directly, so
  the fault is in Qt's drawing, not in font fallback. The wizard's one
  bold site is ItemRow::applyFontScale (src/gui/itemrow.cpp).
  Side note: with no language hint, Chinese text fell back to Noto Sans
  CJK KR, not SC; not yet checked inside the wizard.
  Resolved (2026-10-08): main() sets QT_NO_SYNTHESIZED_BOLD through
  gw::useFontsOwnBold(), so Qt draws the font's own Bold and no more.
  tst_boldtext fails when bold Chinese text has 1.6 times Regular's
  ink or more (1.84 before the fix). A demoreel picture of the zh_CN
  options page shows the titles clean.
  **Layman:** Bold titles in Chinese, Japanese and Korean look blurred and heavy, and greyed-out ones are hard to read.
  Kind: fix.
  Source: in-session-2026-10-08.
  Lanes: translations, gui.

- ✅ [GRND-0044] **Chinese text may be drawn with the Korean font's letter shapes.**
  Seen 2026-10-08 in a probe program while locating GRND-0042: with
  no language hint, Qt drew Chinese text with Noto Sans CJK KR rather
  than SC. Some shared characters are drawn differently in each. Not
  yet checked inside the wizard, which runs with a language chosen.
  Check which CJK font the wizard picks in zh_CN, zh_TW and ja.
  Resolved (2026-10-08): confirmed. Qt picks the face for shared
  characters from the system's LANG; QLocale::setDefault does not
  change it, so an English system drew zh_CN, zh_TW and ja with the
  Korean face. A fallback face is also kept per family for the whole
  run, so a second CJK language got the first one's shapes.
  gw::fontFor() now puts the language's Noto Sans CJK face first, and
  the wizard sets it as the application font for those languages
  only. tst_boldtext became tst_cjktext and checks all four faces in
  one run. Latin words in those languages now use the CJK face's
  Latin letters, as a Chinese desktop does. After choosing one of them,
  a live change to the system font may not be followed until restart;
  not tested.
  **Layman:** Some Chinese characters may show in their Korean shape, which a Chinese reader would notice.
  Kind: investigate.
  Source: in-session-2026-10-08.
  Lanes: translations, gui.

- ✅ [GRND-0045] **Bundle Qt's Wayland plugin in the AppImage.**
  Found in GRND-0013 on Tumbleweed (Plasma on Wayland): the terminal
  printed Qt's "Could not find the Qt platform plugin wayland" and the
  wizard ran through XWayland. It worked, but the AppImage carries no
  wayland platform plugin.
  Resolved (2026-10-10): the AppImage now carries Qt's wayland platform
  plugins and its shell, decoration and graphics plugins. Checked on a
  headless weston: Qt reported loading the wayland plugin, no errors.
  **Layman:** On Wayland desktops the app runs through the older X11 route instead of natively.
  Kind: package.
  Source: release-check-2026-10-08.
  Lanes: packaging.

- ✅ [GRND-0046] **Say the password prompt in plain words, not sudo's raw line.**
  Found in GRND-0013: under "Groundwork needs administrator rights"
  the box shows sudo's own prompt, "[sudo] password for root:" on
  Tumbleweed and "[sudo] password for tester:" on Leap 16. Tumbleweed
  wants root's password, Leap the user's own, so the plain words should
  say which.
  Resolved (2026-10-10, f829538): sudo -p %p passes only whose
  password it wants; the box says it in plain words.
  **Layman:** The password box shows a technical line like "[sudo] password for root:".
  Kind: ux.
  Source: release-check-2026-10-08.
  Lanes: gui.

- ✅ [GRND-0047] **Clear the "Switched off because it needs" note when the needed item is switched back on.**
  Found in GRND-0013 on Leap 16: unticking "Bring the system up to
  date" switched off Media codecs and Flathub with the note "Switched
  off because it needs Bring the system up to date." Ticking the
  update again left both off, and the note stayed, even after Media
  codecs was ticked again by hand.
  Resolved (2026-10-10, 9e5bec8): the note clears once the item it
  names, or the row itself, is switched back. Codecs stays off when the
  update is re-ticked, as design.md says; only the stale note was wrong.
  **Layman:** A row can say it was switched off for a reason that no longer holds.
  Kind: fix.
  Source: release-check-2026-10-08.
  Lanes: gui.

- ✅ [GRND-0048] **Tell the user when a restart is suggested after updates.**
  Found in GRND-0013 on Leap 16: the update installed 391 packages and
  zypper said "Reboot is suggested" because core libraries changed.
  That line appears only under Show details; the summary says "All
  done."
  Resolved (2026-10-10, b95f0e4): after a run that changed anything,
  the Worker asks zypper needs-rebooting; on 102 the run page says a
  restart is suggested.
  **Layman:** After a big update the app says "All done" without saying a restart would be wise.
  Kind: ux.
  Source: release-check-2026-10-08.
  Lanes: gui, core.

- ✅ [GRND-0049] **Open the wizard large enough to show a level's rows without scrolling.**
  Found in GRND-0013: at its opening size on a 1280x960 screen the
  Essentials page showed two of its six rows; maximised, all fit.
  Resolved (2026-10-10, 19826ef): on the first level page shown, the
  window grows to fit the fullest level's rows, within the screen.
  Not yet seen on a real desktop (2026-10-10): demoreel's display
  maximises every window, and weston-screenshooter hung on a headless
  weston. tst_wizard's aLevelsRowsShowWithoutScrolling is the only proof.
  Look at it on the next VM or desktop check.
  **Layman:** The window opens small, so most choices on a page are hidden until you scroll.
  Kind: ux.
  Source: release-check-2026-10-08.
  Lanes: gui.

## 0.2.0 — Checked languages and colour themes

Translations, asked for by the user on 2026-10-02: Asian and right-to-left
languages, and Afrikaans. The machinery and every draft ship in 0.1.0;
each translation item here closes once its check is done. Colour themes
were added on 2026-10-10 (GRND-0050).

- 🚧 [GRND-0033] **Translation: Afrikaans.**
  Serves S3. The user is South African and can check this one.
  Progress (2026-10-07): every string has an Afrikaans draft (e7dd36e);
  waits for the user's check before it ships as checked. Found and fixed
  on the way: counted sentences had one form, so counts above one showed
  English in every language (d13308d); Qt has no Afrikaans button words,
  so the app now labels its own buttons (e7dd36e).
  Progress (2026-10-08): the user checked A to E with Gemini and sent
  wording (applied, 70802b1, 831fcfc); F to T went through Gemini in the
  user's Chrome (0a2bef0). User's decisions: informal "jy"; "OK" stays
  "OK"; the name stays "Groundwork"; "opdatering" for update everywhere;
  progress messages as "Besig om ... te ..."; Afrikaans KEEPS its draft
  mark until a fluent person reads it, since Gemini and Claude are not
  native speakers. Done means: that read, then add "af" to kChecked in
  src/core/translations.cpp and flip this item.
  **Layman:** The whole app in Afrikaans.
  Kind: implement.
  Source: user-request-2026-10-02.
  Lanes: translations.
  Blocked-by: GRND-0032, GRND-0008, GRND-0038.

- 🚧 [GRND-0034] **Translations: Arabic and Hebrew, right to left.**
  Serves S3 and S4. Proves the mirrored layout on every page.
  Progress (2026-10-08): both drafted by Claude and filled. Arabic is
  Modern Standard; Hebrew instructions use the infinitive so they
  address no gender. Mirrored layout checked by picture on all six
  wizard pages, both languages, up to the review page. Lines starting
  with Latin text got a right-to-left mark (CLAUDE.md, Translations).
  Next: the Gemini check, which has not run yet.
  Gemini check (2026-10-08, Gemini 3.1 Pro): Arabic took four plural
  fixes (11 and up count with a singular noun, so the verb agrees in the
  singular); Hebrew took two rewordings. Rejected: dropping "יש" before
  instructions, and %n in place of "one" in Hebrew singulars. Still
  drafts until a native speaker reads them.
  **Layman:** The app in Arabic and Hebrew, laid out right to left.
  Kind: implement.
  Source: user-request-2026-10-02.
  Lanes: translations, gui.
  Blocked-by: GRND-0032, GRND-0008, GRND-0038.

- 🚧 [GRND-0035] **Translations: Chinese (Simplified and Traditional), Japanese and Korean.**
  Serves S3. Check that a fresh install shows these scripts; if a font
  is missing, the app says so rather than showing empty boxes.
  Traditional Chinese (zh_TW, as read in Taiwan and mostly in Hong Kong)
  added at the user's request on 2026-10-08. Written Cantonese was not
  chosen: software uses standard written Chinese. Font check: this
  openSUSE install has no Chinese, Japanese, Korean or Hindi font, and
  the default "fonts" pattern installs none.
  Progress (2026-10-08): zh_CN, zh_TW, ja and ko drafted by Claude
  (d387222); full gate passed. The no-font test's disabled branch now
  runs and was proved red once. Gemini check still owed: Pro was busy
  and the request failed twice.
  Progress (2026-10-08, later): Gemini 3.1 Pro tried on zh_CN; it
  answered "Sorry, something went wrong", then an empty reply. Stopped
  after two. CJK fonts are now installed, so the four can be read on screen.
  Progress (2026-10-08, 14:35): Pro tried twice more on zh_CN. First
  reply came from a fallback model ("Pro is in high demand ... Another
  model was used"), so not counted; the retry gave no reply in 5.5
  minutes. Gemini check still owed. Looked on screen instead: welcome
  page and all four options pages draw in zh_CN, zh_TW, ja and ko, every
  character shown, nothing clipped. One fault found: bold CJK text is
  smeared (GRND-0042).
  **Layman:** The app in Chinese, Japanese and Korean.
  Kind: implement.
  Source: user-request-2026-10-02.
  Lanes: translations.
  Blocked-by: GRND-0032, GRND-0008, GRND-0038.

- 🚧 [GRND-0036] **Translation: Hindi.**
  Serves S3. Same font check as the East Asian item.
  Progress (2026-10-08): Hindi drafted by Claude, marked draft; full gate
  passed. Noto Devanagari and CJK fonts installed, with the user's leave.
  Gemini check owed: Pro failed twice today on zh_CN, so all checks
  (zh_CN, zh_TW, ja, ko, hi) wait for one batch.
  Progress (2026-10-08, 14:55): looked on screen. Welcome page and
  options pages draw in Hindi, every character shown, nothing clipped.
  Gemini check still owed: Pro failed every try today.
  **Layman:** The app in Hindi.
  Kind: implement.
  Source: user-request-2026-10-02.
  Lanes: translations.
  Blocked-by: GRND-0032, GRND-0008, GRND-0038.

- 🚧 [GRND-0037] **Translations: German, French, Spanish and Portuguese.**
  Serves S3.
  Progress (2026-10-08): all four drafted by Claude and filled. German uses "Sie",
  French "vous", Spanish (Spain) "tú", Portuguese is Brazilian (the file's
  pt_BR) with "você". Each ships marked draft; a Gemini check is next.
  Gemini check (2026-10-08), all four: German took three rewordings
  (one rejected: it named Google's browser "Google"); French and Spanish
  needed none; Portuguese took "em andamento…" for "working…". Still
  drafts until a native speaker reads them.
  **Layman:** The app in four widely used European languages.
  Kind: implement.
  Source: user-request-2026-10-02.
  Lanes: translations.
  Blocked-by: GRND-0032, GRND-0008, GRND-0038.

- 🚧 [GRND-0040] **Translations: isiZulu and isiXhosa.**
  Serves S3. Added at the user's request on 2026-10-07.
  Progress (2026-10-08): isiZulu and isiXhosa drafted by Claude, marked
  draft; full gate passed. Choices are in each table's header comment
  (tr_zu.py, tr_xh.py in the handoff tools folder). Gemini check owed,
  batched with zh_CN, zh_TW, ja, ko and hi.
  Progress (2026-10-08, 14:55): looked on screen. Welcome page and
  options pages draw in isiZulu and isiXhosa; the long lines fit and
  nothing is clipped. Gemini check still owed: Pro failed every try
  today.
  **Layman:** The app in isiZulu and isiXhosa.
  Kind: implement.
  Source: user-request-2026-10-07.
  Lanes: translations.
  Blocked-by: GRND-0032, GRND-0008, GRND-0038.

- ✅ [GRND-0050] **Offer colour themes, dark ones and high contrast among them, following the desktop unless one is chosen.**
  The user asked on 2026-10-10: they are partially sighted, light
  sensitive, and prefer dark mode. Decided that day: the window opens
  dark when the desktop is dark, light otherwise, and the first page
  offers Light / Dark / Follow desktop to override it. It goes in the
  release after 0.1.0, not in 0.1.0.
  Widened (2026-10-10, user): more themes than light and dark,
  matching the user's other projects. Survey that day: the names most
  shared are Follow system, Light, Dark, High contrast light and dark,
  Midnight, Emerald, Nord, Dracula, Solarized Dark, Gruvbox, Monokai,
  Tokyo Night and Catppuccin. The C++ source to copy from is
  Ants_Terminal's src/themes.h and src/themes.cpp (eleven named themes,
  a contrast check, follow-desktop wiring in mainwindow.cpp). The
  contrast-tuned palettes, high-contrast pair included, are in
  LocalWebServerManager's src/lwsm/theme.py (Python, values reusable).
  Resolved (2026-10-10): the first page offers Colours under Language:
  Follow the desktop (the default), Light, Dark, High contrast light
  and dark, Midnight, Emerald, Nord, Dracula, Solarized Dark, Gruvbox,
  Monokai, Tokyo Night and Catppuccin Latte. Following a dark desktop
  whose Qt colours are light uses the Dark theme. The choice lasts for
  the run, like the language, and reaches the password box.
  tst_themes checks every theme's text at 4.5:1 (7:1 for high
  contrast); it caught Catppuccin Latte's blue, now darkened.
  Seen on demoreel's display 2026-10-10: dark by default, High
  contrast dark switched in place. Not yet seen: the AppImage on a dark
  desktop, and the password box in a chosen theme.
  **Layman:** The app can be dark, which is easier on light-sensitive eyes; it follows the desktop unless you choose otherwise.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: gui.

- ✅ [GRND-0051] **Drop "not done" from the update row, which read as a contradiction beside "no updates were waiting".**
  Seen in the 0.1.0 Essentials screenshot by the website session: "not
  done — No updates were waiting at the last refresh of the software
  sources." The update is a preparation and is always not done by
  design, so the word says nothing there. Its row, and check mode's
  line, now show the detail alone.
  Resolved (2026-10-10): core's stateLine() now builds the line for
  both the row and check mode; a not-done preparation shows its detail
  alone.
  Owed at the next release: send the website session a fresh Essentials
  screenshot showing the new wording (promised 2026-10-10).
  **Layman:** The update row no longer says "not done" while also saying no updates are waiting.
  Kind: ux.
  Source: website-session-2026-10-10.
  Lanes: gui, core.

## 0.3.0 — A machine that keeps running

Simple on/off items drawn from the owner's own machine (decided 2026-10-10):
they keep a computer responsive, tidy and ready for games, and set Ants Terminal
as the terminal.

- ✅ [GRND-0052] **Item: RAM protection, so a full memory closes the greediest app before the desktop freezes.**
  From the owner's machine (SYSTEM_OPTIMISATIONS.md, section 10):
  systemd-oomd enabled, the memory controller delegated to the user
  session, a memory floor and higher CPU and I/O weight for the
  desktop's session.slice, and app.slice made the only kill candidate.
  On Tumbleweed systemd-oomd came from systemd-experimental; check
  that and Leap 16 when built. The user called this the main ask.
  Resolved (2026-10-10): memoryitem.cpp, on the System setup page.
  systemd-oomd is in systemd-experimental on both Tumbleweed (rpm -qf,
  this machine) and Leap 16.0 (its oss repository lists it). The
  desktop's memory floor scales: 2G from 8 GiB, 1G from 4 GiB, else
  512M. The check reads any .conf file in the drop-in folders, so
  protection set up by hand counts: check mode says "already done" on
  the owner's machine. New for every item: a step may carry text for
  its command's input, and writeFileStep writes a file with dd.
  Not yet run for real on a fresh machine.
  **Layman:** When memory runs out, the desktop stays usable and the app using the most memory is closed, instead of the whole computer locking up.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items.

- ✅ [GRND-0053] **Item: the emergency keyboard escape, Alt+SysRq, fully on.**
  From SYSTEM_OPTIMISATIONS.md, section 8: kernel.sysrq = 1 in a
  sysctl.d file. The distro default was 184, which leaves out sync,
  unmount, reboot and the OOM kill.
  Resolved (2026-10-10): sysrqitem.cpp, on the System setup page.
  Correction to the note above: by the kernel's bit list (repeated in
  /usr/lib/sysctl.d/50-default.conf), 184 has sync, unmount and reboot;
  it lacks the keyboard (R), signals (E, I) and the OOM kill (F). The
  check counts 1, or any mask holding 4+16+32+64+128. It writes
  /etc/sysctl.d/99-groundwork-sysrq.conf, which sorts after
  50-default.conf, then runs sysctl -p on it. Check mode on the owner's
  machine says "already done". Not yet run for real on a fresh machine.
  **Layman:** A frozen computer can be restarted safely from the keyboard instead of the power button.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items.

- ✅ [GRND-0054] **Item: a smoother scheduler for spinning hard drives.**
  Generalised from SYSTEM_OPTIMISATIONS.md, section 9, which set BFQ
  on one SMR drive by serial. Here: a udev rule setting bfq on every
  drive whose queue/rotational is 1; SSDs keep their scheduler.
  Offered only where a spinning drive is found.
  Shipped 2026-10-10: DiskSchedulerItem writes
  /etc/udev/rules.d/60-groundwork-iosched.rules, matching whole sd*
  disks with queue/rotational 1, then reloads udev and triggers a
  change on the spinning drives. Optical and loop devices also report
  rotational, so only sd* counts. Not needed where no spinning drive is
  found. tst_diskscheduleritem.
  **Layman:** While a slow hard drive is busy, the desktop stays smooth.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items.

- ✅ [GRND-0055] **Item: calmer wake from hibernation, with maintenance timers spread out.**
  From SYSTEM_OPTIMISATIONS.md, section 3: RandomizedDelaySec=30min
  drop-ins on the catch-up-prone daily and weekly timers (snapper,
  logrotate, backup-rpmdb, backup-sysconfig and the like). Name only
  timers that exist on the machine.
  Shipped 2026-10-10: WakeItem adds a RandomizedDelaySec=30min
  drop-in (99-groundwork-spread.conf) only to listed timers that have
  no delay. backup-rpmdb, backup-sysconfig and check-battery already
  ship 2h and logrotate 1h, so the notes' 30min drop-ins shortened
  them; this item never does. packagekit-background added to the list
  (no delay, observed in the storm). tst_wakeitem.
  **Layman:** After waking, the computer no longer runs every missed maintenance job at once and slows to a crawl.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items.

- ✅ [GRND-0056] **Item: cap the system log at 1 GB.**
  From SYSTEM_OPTIMISATIONS.md, section 11: a journald.conf.d drop-in
  with SystemMaxUse=1G, SystemKeepFree=2G, SystemMaxFileSize=128M and
  MaxRetentionSec=3month. Check journald's real default when built;
  the notes' 10% figure is unverified.
  Shipped 2026-10-10: LogCapItem writes
  /etc/systemd/journald.conf.d/99-groundwork-log-cap.conf with the
  four settings, then restarts systemd-journald (restart, not reload:
  journald reloads on SIGHUP only from systemd 258). Done when the
  SystemMaxUse systemd would use is 1G or less, read from journald.conf
  and its drop-ins in systemd's order. journald's real default is 10%
  of the filesystem capped at 4G (journald.conf(5), systemd 261), so
  the notes' "~20 GB here" was wrong. The notes' Storage=persistent
  was left out: not part of this item. tst_logcapitem.
  **Layman:** The system's log can no longer grow large enough to fill a small drive.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items.

- ✅ [GRND-0057] **Item: a shorter wait at the boot menu.**
  From SYSTEM_OPTIMISATIONS.md, section 6: sdbootutil set-timeout 3,
  from 8. Find the right command for each bootloader openSUSE installs
  (systemd-boot, grub2-bls, grub2) when built.
  Shipped 2026-10-10: BootMenuItem reads LOADER_TYPE from
  /etc/sysconfig/bootloader. systemd-boot and grub2-bls: the wait is
  the firmware variable LoaderConfigTimeout (anyone can read it);
  without it the wait is on the root-only EFI partition, which nothing
  here can read (the Worker runs as the user too), so the item reads
  not done and the apply sets the variable; apply is `sdbootutil
  set-timeout 3`, which
  also updates TPM predictions. grub2 and grub2-efi: GRUB_TIMEOUT in
  /etc/default/grub (5 when absent), set by sed, then grub2-mkconfig.
  Done at 0 to 3 seconds or a hidden menu. The owner's machine
  (grub2-bls) already has 3. tst_bootmenuitem.
  **Layman:** The computer starts a few seconds faster, with the boot menu still reachable.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items.

- ✅ [GRND-0058] **Item: the freeze recorder, which notes what was using memory before a freeze.**
  From SYSTEM_OPTIMISATIONS.md, section 11: a protected service
  sampling memory and I/O pressure every 10 s, logging only when
  pressure rises, to the journal and a size-capped file under
  /var/log. Its script must ship inside Groundwork; no shell steps.
  Shipped 2026-10-10: FreezeRecorderItem writes the script,
  src/items/freeze-recorder.sh (built in at :/items), to
  /usr/local/bin/freeze-recorder and the owner's hardened unit to
  /etc/systemd/system/freeze-recorder.service, then daemon-reload,
  enable and restart. Same file names as the owner's, so the owner's
  machine reads done. Changes from the owner's script: it covers every
  logged-in user, not only uid 1000, and PROC lets the test feed fixed
  readings. The gate now shellchecks src/*.sh. tst_freezerecorderitem
  runs the script once under fixed pressure, calm and ring trimming.
  **Layman:** If the computer freezes, a small log shows afterwards what was eating its memory.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items.

- ✅ [GRND-0059] **Item: less eager swapping.**
  From the owner's machine: vm.swappiness=10 in a sysctl.d file,
  from 60. Chosen by the user over the recommendation to leave it
  out (2026-10-10).
  Shipped 2026-10-10: SwappinessItem reads /proc/sys/vm/swappiness;
  done at 10 or lower. Apply writes
  /etc/sysctl.d/99-groundwork-swappiness.conf and runs sysctl -p on
  it, as SysrqItem does. The owner's machine reads done (its own
  99-claude-tuning.conf sets 10). tst_swappinessitem.
  **Layman:** The computer keeps programs in memory longer before moving them to disk.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items.

- ✅ [GRND-0060] **Item: NumLock on at login.**
  From the owner's set-up script: Numlock=on in /etc/sddm.conf.d.
  Offered only where SDDM is the login screen.
  Shipped 2026-10-10: NumlockItem, in Configuration. SDDM is the
  login screen when /etc/alternatives/default-displaymanager points at
  sddm (openSUSE's way) or display-manager.service at sddm.service.
  Reads [General] Numlock= in SDDM's order (sddm.conf(5): system
  folder, /etc/sddm.conf.d, /etc/sddm.conf; default none). Apply
  writes /etc/sddm.conf.d/99-groundwork-numlock.conf; no SDDM restart,
  which would end the session. The owner's machine reads done.
  tst_numlockitem.
  **Layman:** The number pad works straight away at the login screen.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items.

- ✅ [GRND-0061] **Item: Ants Terminal, set as the terminal.**
  The owner's own terminal, offered to everyone (user, 2026-10-10).
  Installed from its OBS repository, home:milnet:ants-terminal, which
  builds for Tumbleweed and Leap 16 (Ants Terminal README, Install).
  Setting it as the desktop's terminal runs as the user, not root.
  Shipped 2026-10-10: AntsTerminalItem, in Nice to have. Repository
  home:milnet:ants-terminal, openSUSE_Tumbleweed or openSUSE_Leap_16.0
  (checked 2026-10-10: Slowroll and Leap 16.1 answer 404, so those read
  not needed). A repository already pointing there is reused; the key
  is trusted at refresh, as with Packman. On KDE (kwriteconfig6
  present), two user steps set kdeglobals [General]
  TerminalApplication=ants-terminal and
  TerminalService=za.co.antsprojectshub.AntsTerminal.desktop, and the
  check requires the latter. The owner's machine reads not done: it
  runs a development build, not the package. tst_antsterminalitem.
  **Layman:** Installs Ants Terminal and makes it the terminal the desktop opens.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items.

- ✅ [GRND-0062] **Item: video acceleration on AMD graphics.**
  From the owner's machine: Mesa's VA-API driver for radeonsi.
  Offered only where an AMD graphics card is found. Check what the
  media codecs item already brings in from Packman.
  Finding 2026-10-10, before building: the codecs item brings in no
  Mesa package. Packman's Mesa-libva, the one that decodes H.264 and
  HEVC, is only in the full Packman repository; Essentials carries no
  Mesa package (checked against both repositories' package lists).
  ADR-0003 rules the full repository out. openSUSE's own Mesa-libva
  leaves those formats out. Waiting on the user: openSUSE's driver
  only, an ADR-0003 exception for Mesa, or drop the item.
  Shipped 2026-10-12, under ADR-0004: AmdVideoItem, Essentials, after
  NVIDIA. Offered where a PCI device of vendor 0x1002 has a display
  class (0x03...). Depends on media-codecs, whose dup switches Mesa to
  Packman's builds; then installs Mesa-libva --from the Packman alias
  with --allow-vendor-change. Done when Mesa-libva's vendor names
  Packman. Check mode on the owner's machine: already done. Not run
  here: an install on a fresh system. tst_amdvideoitem.
  **Layman:** Videos play using the graphics card, which is smoother and saves power.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items.

- ✅ [GRND-0063] **Item: gaming additions — game controllers, and SELinux's gaming policy.**
  Adds to the gaming group (GRND-0026, which installs Steam and
  Bottles from Flathub). Flatpak Steam needs the host's controller
  rules (steam-devices). Where SELinux is enforcing, the owner's
  machine has selinux-policy-targeted-gaming. GameMode too, if Flatpak
  Steam can use the host's.
  Shipped 2026-10-10: GamingExtrasItem, after Bottles. Installs
  steam-devices and gamemoded, plus selinux-policy-targeted-gaming
  where /sys/fs/selinux/enforce reads 1; all from openSUSE's main
  repository. GameMode: the daemon is the gamemoded package (gamemode
  holds only the tools). Flatpak apps reach it through the portal's
  org.freedesktop.portal.GameMode interface, which this machine's
  portal offers; that Flatpak Steam uses it end to end was not run
  here (Flatpak Steam is not installed). tst_gamingextrasitem.
  **Layman:** Game controllers work in Steam, and Windows games run where SELinux would block them.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items.

- ✅ [GRND-0064] **Item: virtual machines.**
  From the owner's machine: QEMU/KVM. Offered where the processor
  supports virtualisation. Pick the openSUSE pattern and a graphical
  manager when built.
  Shipped 2026-10-12: VirtualMachinesItem, after the gaming additions.
  Offered where a flags line in /proc/cpuinfo lists vmx or svm as a
  whole word; otherwise "not needed here". Installs openSUSE's kvm_tools
  pattern (main repository), which requires kvm_server, libvirt and
  virt-manager. Done when virt-manager and libvirt-daemon-qemu are
  installed. openSUSE's systemd presets enable virtqemud.service and
  libvirt's sockets on install, so the sentence says it is ready after a
  restart. Not run here: an install, and whether virt-manager then asks
  for a password for qemu:///system. tst_virtualmachinesitem.
  **Layman:** Lets the computer run other operating systems in a window.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items.

- ✅ [GRND-0069] **Use on/off switches instead of tick boxes in every row.**
  The user prefers switches to tick boxes (2026-10-10). Qt has no switch
  widget, so ItemRow gets a small painted one: it keeps the item's
  title as its label, works from the keyboard, grows with the font
  (as the enlarged tick box does now, itemrow.cpp), follows the
  colour theme, and reports itself to screen readers as a checkable
  control. Applies to every row, the 0.4.0 lists of choices included.
  Shipped 2026-10-10: src/gui/switch.* paints a QCheckBox as a switch,
  so keyboard and screen-reader behaviour are a tick box's. Test
  eachRowIsAnOnOffSwitch (tst_wizard) proven red on the tick box, then
  green. Every theme, the disabled state and right-to-left were checked
  by eye in an offscreen render.
  **Layman:** Each choice is an on/off switch, like a light switch, instead of a tick box.
  Kind: ux.
  Source: user-request-2026-10-10.
  Lanes: gui.

- ✅ [GRND-0070] **Trailer footage for the website, after 0.3.0 is published.**
  The website session asked for 3 to 6 demoreel clips plus notes.md
  (hook lines, tagline, captions, feature lines, platforms, sound).
  Brief: ~/.local/share/claude-handoff/ants-projects-hub-website-trailers-2026-10-10.md
  (shared; do not delete). Deliver to /mnt/Games/Trailers/incoming/groundwork/,
  then message ants-projects-hub-website. Deferred until 0.3.0 is out
  because it replaces tick boxes with switches; told the website session so.
  Released features only, demo data only (check mode prints the hostname).
  Resolved (2026-10-13): five clips and notes.md delivered to
  /mnt/Games/Trailers/incoming/groundwork/, recorded from the 0.3.0
  build; ants-projects-hub-website told. The Configuration page, which
  shows the hostname, is cut from every clip.
  **Layman:** A short video of Groundwork for its web page, recorded once the switches ship.
  Kind: marketing.
  Source: peer-request-2026-10-10 ants-projects-hub-website.

- ✅ [GRND-0071] **Packman: the full repository, with the system switched to its builds.**
  The user ruled on 2026-10-12 that a repository rule applies to
  everything: where software is in another repository, use it. ADR-0004
  supersedes ADR-0003 (Essentials only). The codecs item adds the full
  Packman repository and switches the system to Packman's builds, as
  opi 5.16.0 does (zypper dup --from packman --allow-downgrade
  --allow-vendor-change). Unblocks GRND-0062.
  Shipped 2026-10-12: packmanAlias now counts only a full Packman
  repository; packmanSteps adds it (alias packman, priority 90) where
  none is configured, leaving Essentials in place. The codecs item adds
  opi's dup step (zypper -n dup --from <alias> --allow-downgrade
  --allow-vendor-change) before installing the codecs, and reads "not
  done" on an Essentials-only system. Broadcom on Leap installs from the
  full repository too. Not run here: the dup on a fresh system.
  **Layman:** Groundwork uses Packman's full software source, so programs and drivers it offers are not held back.
  Kind: feature.
  Source: user-request-2026-10-12.
  Lanes: items.

- ✅ [GRND-0074] **Item: OneUp, offered beside the built-in updates.**
  From the user, 2026-10-12: offer the owner's OneUp
  (/mnt/Games/Scripts/Linux/OneUp) as an option for updates. It sits
  beside openSUSE's own updates and never replaces them: removing those
  would break things. OneUp installs from the home:milnet OBS repository
  (OneUp README, Install & run), as Ants Terminal does; copy
  AntsTerminalItem.
  Shipped 2026-10-12: OneUpItem, Nice to have, after Ants Terminal.
  Offered on Tumbleweed and Leap 16.0; home:milnet answers 404 for
  Slowroll and Leap 16.1 (checked 2026-10-12), and its package list holds
  oneup on both built systems. Adds the repository as home_milnet, the
  alias the README's .repo file gives, unless a repository at
  home:/milnet/ is already there; then refreshes and installs oneup.
  Touches nothing of openSUSE's own updates. Not run here: an install.
  tst_oneupitem.
  **Layman:** Offers OneUp, which updates everything on the computer with one click.
  Kind: feature.
  Source: user-request-2026-10-12.
  Lanes: items.

- ✅ [GRND-0075] **Keep the window on screen on Wayland, so Back, Next and Apply can be clicked.**
  Found in the fresh-Tumbleweed VM check before 0.3.0 (snapshot
  20261011, KDE on Wayland, 1280x1024). From the System setup page on,
  the window runs past the screen's bottom edge and its buttons cannot
  be clicked; only Alt+N and Enter reach them. Reproduced without
  touching the window.
  Cause: Wizard::fitRows (src/gui/wizard.cpp) grows the window, then
  calls move() to pull it up. Wayland lets no window move itself, so
  move() does nothing. Qt also reports no title bar height there, so the
  height cap can overshoot by a title bar.
  Planned fix: on Wayland, hide() and show() after growing so the
  desktop places the window afresh; leave room for a title bar when Qt
  reports none. Proof is the VM: buttons visible after the fix.
  The user ruled 2026-10-12: fix before releasing 0.3.0.
  VM check, rest of the run, all passed: system update; codecs (Packman
  added, dup ran clean, ffmpeg/vlc/Mesa/libheif from Packman, gst plays
  H.264, H.265, AAC, zypper verify clean); boot menu on systemd-boot
  (LoaderConfigTimeout EFI variable = 3); virtual machines; OneUp.
  AMD video not testable (no AMD GPU in the VM). Note, not a defect:
  the codecs install names gstreamer-plugins-bad/ugly/libav with --from
  packman; Packman lacks those names, so zypper took openSUSE's, which
  play everything on Packman's libavcodec.
  Resolved 2026-10-13: fitRows hides and re-shows the window on Wayland
  after growing it, and leaves room for a title bar Qt cannot see.
  Re-checked in the same VM: Essentials, System setup and
  Configuration each show the whole window, and a mouse click on Next
  works.
  **Layman:** On a fresh install the wizard's window grew past the bottom of the screen, hiding its buttons.
  Kind: fix.
  Source: vm-release-check-2026-10-12.
  Lanes: gui.

## 0.4.0 — Your own set-up

Items that need the user's own choices in the window (decided 2026-10-10). Each
has a written design before it is built.

- 📋 [GRND-0065] **Item: your GitHub projects, copied into a folder you choose.**
  The user liked this over a personal list (2026-10-10): sign in
  once, list the user's repositories, tick which to copy, choose the
  folder. Private ones work once signed in. Copying runs as the user,
  not root. Needs a design first: signing in from a window, a list of
  choices rather than one switch, and nothing personal in the source.
  **Layman:** Sign in to GitHub once, tick your projects, and Groundwork copies them onto the new computer.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items, gui.

- 📋 [GRND-0066] **Item: share folders on the network.**
  From the owner's machine: Samba (smb and nmb) with wsdd so Windows
  finds it, and the firewall opened for it. Needs a design first: which
  folders, who may open them, and a Samba password.
  **Layman:** Lets other computers at home open folders you choose on this one.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items, gui.

- 📋 [GRND-0067] **Item: mount extra drives at start-up.**
  From the owner's machine: fstab entries by UUID with nofail,
  noatime, nosuid, nodev and x-gvfs-show, so a missing drive never
  stops the boot. Needs a design first: which drives, where they
  appear, and never touching a drive in use.
  **Layman:** Second hard drives show up and are ready every time the computer starts.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items, gui.

- 📋 [GRND-0068] **Item: hibernate instead of sleep, where sleep is unreliable.**
  From SYSTEM_OPTIMISATIONS.md, sections 1 and 2, and the owner's set-up
  script: resume= on the kernel command line, HibernateMode=shutdown,
  suspend masked. Needs a design first: swap at least as large as
  memory, Secure Boot's lockdown blocks hibernation, and a wrong
  resume= can stop the machine waking. Pairs with GRND-0055.
  **Layman:** The computer can save everything and switch off, then carry on where it left off, for machines where sleep does not work.
  Kind: feature.
  Source: user-request-2026-10-10.
  Lanes: items, gui.

- 📋 [GRND-0072] **Remember the chosen colour theme between runs.**
  The user ruled on 2026-10-12: yes. Their reason: Groundwork will hold
  more of the user's own settings over time. Design with write-spec
  first, beside the scan-and-restore item.
  **Layman:** The wizard opens in the colour theme you picked last time.
  Kind: feature.
  Source: user-request-2026-10-12.
  Lanes: gui.

- 📋 [GRND-0073] **Scan a system's settings, and put them back after a reinstall.**
  From the user, 2026-10-12: Groundwork should scan the system for its
  configurations, so that after a reinstall it can reinstate them.
  Needs a write-spec design first: what is recorded, where the record
  lives so it survives a reinstall, and how restoring maps onto items.
  **Layman:** Groundwork notes how your computer is set up, so a fresh install can be set up the same way.
  Kind: feature.
  Source: user-request-2026-10-12.
  Lanes: items, core.

## 0.5.0 — Ready for the next install

Preparing a drive and carrying a setup over (discovery S5, S6; design.md,
Preparing a drive and Carrying a setup over). Split from GRND-0076 on
2026-10-15.

- ✅ [GRND-0077] **First page offers three jobs.**
  Serves S5 and S6 (design.md, The shape in one paragraph). Set up this
  machine, carry this setup over, prepare a drive. On a live system only
  preparing is offered: detect a live system. Measure first, in a VM,
  whether the AppImage starts on openSUSE's live images (it needs FUSE 2,
  which a live image may lack); if it does not, say how the prepare job
  reaches a live system. No spec: one subsystem, cheap to redo.
  Done (2026-10-15). Measured in a VM: the 0.3.1 AppImage starts on
  Tumbleweed's XFCE live image and its Rescue CD. Both carry fuse3 and
  libfuse2, and the AppImage's runtime needs neither library. Leap 16
  publishes no live image. A live system is told by rd.live.image in
  /proc/cmdline (isLiveSystem). The first page offers the jobs that are
  built, as a choice once there are two; a live system is offered only
  preparing a drive, and until that is built the page says there is
  nothing to do. GRND-0076 and GRND-0078 each add their job to
  WizardSetup::jobs.
  **Layman:** Groundwork's first page lets you choose: set up this machine, carry its setup to a new install, or prepare a drive.
  Kind: feature.
  Source: user-request-2026-10-15.
  Lanes: wizard, core.

- 🚧 [GRND-0076] **Prepare a drive for installing openSUSE.**
  User request (2026-10-15): a second job beside first-run setup. The
  two work alone; when both are used, the first informs the second.
  Answers given the same day:
  - Runs from a live USB stick and on a working Linux machine
    preparing another drive.
  - Gathers what the machine has (graphics, Wi-Fi and the like) and the
    user's choices into a saved file; setup reads it after the install
    and sets the switches.
  - Suggests partition layouts; the user may build a custom one instead.
  - Where Windows is on the drive, advise before anything shrinks:
    back up, turn off Fast Startup, check for BitLocker, shrink from
    Windows' own Disk Management. Defragmenting is optional, for spinning
    disks (the user asked for it; it eases shrinking, it protects nothing).
  - The openSUSE installer stays a full alternative; preparing is
    optional.
  Changes scope: brief.md and discovery.md both say "Not an installer".
  Update those first, then design it with write-spec (wiping a drive
  cannot be undone).
  Widened (2026-10-15, user): on an installed machine, Groundwork
  gathers that machine's setup, asks which parts to keep for the next
  install, and saves them. After the install, setup starts from that
  file, plus Groundwork's usual items. Parts carried: Groundwork's own
  items, programs the user added, extra software sources, and desktop
  and app settings (lifts "no dotfile management"). Discovery amended
  and agreed the same day: S5 (prepare a drive), S6 (carry a setup
  over), and "Not a backup tool". Next: write-spec.
  Split (2026-10-15): design.md amended and agreed by the user. This
  item is now preparing a drive only (design.md, Preparing a drive; S5).
  The three-job first page is GRND-0077, gathering and saving a setup
  GRND-0078, starting from a saved setup GRND-0079. Wiping is confirmed
  with a switch (user, 2026-10-15). Needs a spec first (write-spec).
  Spec accepted (2026-10-15): docs/specs/GRND-0076-prepare-a-drive.md.
  User rulings the same day: beside Windows, a new start-up partition;
  room to hibernate on every layout, starting on when memory-sized swap
  is at most 5 % of the space, with the share stated otherwise.
  Progress (2026-10-15): stage 1 of 4 committed (eb716bc, not pushed):
  src/drives/ and tst_drives, 31 passing; scripts checked with real
  sfdisk on image files. Stage 2 written but UNCOMMITTED:
  Worker::prepareDrive and --worker --prepare-drive FILE (worker.cpp/.h,
  main.cpp, CMakeLists.txt), plus partitionSizes and addedPartitions in
  drives. It builds; it has no tests yet; tst_translations fails until
  update_translations runs and the new gw::Drives / gw::Worker strings
  get translations (it also reports a context named "gw", not yet
  diagnosed). Next: tst_worker cases for INV-3, INV-4, INV-6 with fake
  lsblk, sfdisk, udevadm and mkfs.*; then stage 3, the wizard's prepare
  pages (INV-2) and Job::PrepareDrive in main.cpp's jobs; then stage 4,
  a VM run. Live images and 64 GiB / 100 GiB test images are in
  /mnt/Emulators/claude-scratch/grnd-live; the QMP helper is
  /mnt/Emulators/claude-scratch/grnd-vm/qmp.py.
  Progress (2026-10-15, later): stage 2 committed. tst_worker covers
  INV-3, INV-4 and INV-6 with fake lsblk, sfdisk, udevadm and mkfs.*,
  plus a whole-drive run that ends ok; each guard was broken in turn and
  its test went red. The "gw" context was Drives' free functions calling
  a local tr(), which lupdate filed under "gw"; each call now names
  gw::Drives. New strings drafted in all 14 languages.
  worker-interface.md names --prepare-drive. Next: stage 3, the wizard's
  prepare pages (INV-2: Apply needs the "I understand" switch) and
  Job::PrepareDrive in main.cpp's jobs; then stage 4, a VM run.
  Progress (2026-10-16): stage 3 committed and pushed (42fa72a). The
  first page offers preparing a drive; its pages are the drive, Windows'
  checklist, the layout (suggestions or custom, with the
  room-to-hibernate switch) and confirm, whose Apply waits for "I
  understand" (INV-2). The Worker prints what to set in the installer
  per new partition (spec 4.6). Five tst_wizard cases and one tst_worker
  check, each seen red; gate 45 of 45. 52 new strings drafted in all 14
  languages, not yet looked at right to left. Next: stage 4, a VM run on
  a blank virtual disk (whole drive with separate home), then NTFS plus
  free space (beside), each followed by the installer.
  **Layman:** Groundwork can get a new drive ready before openSUSE is installed, and remember what it found to make setup afterwards smoother.
  Kind: feature.
  Source: user-request-2026-10-15.
  Lanes: core, wizard, packaging.
  Blocked-by: GRND-0077.

- 📋 [GRND-0078] **Gather a setup and save it to a file.**
  Serves S6 (design.md, Carrying a setup over). Gathers Groundwork's done
  items, programs zypper marks as asked for (i+), Flatpak apps, added
  software sources, and desktop and app settings app by app. Settings
  holding secrets start switched off and warn when switched on (user
  ruling 2026-10-15). Needs a spec first (write-spec): the saved file's
  format is a contract between two jobs and two installs.
  Spec accepted (2026-10-15): docs/specs/GRND-0078-saved-setup-file.md.
  Claude's calls there, open to the user: an uncompressed tar file,
  .local/share mostly left out, settings over 100 MiB start off.
  **Layman:** Groundwork can list what is set up on this machine, let you keep or drop each part, and save it for your next install.
  Kind: feature.
  Source: user-request-2026-10-15.
  Lanes: carry, wizard.
  Blocked-by: GRND-0077.

- 📋 [GRND-0079] **Start a new install from a saved setup.**
  Serves S6 (design.md, Carrying a setup over). The setup job's first
  page offers a saved file; kept items start switched on; programs and
  sources become items, sources first; settings are copied as the user
  after the programs, keeping any file they replace. Where the file holds
  secrets, the last page warns again and offers to remove them or delete
  the file, only on the user's say-so. Built under the saved file's spec
  from the item that gathers a setup.
  **Layman:** After reinstalling, Groundwork can read your saved file and switch on everything you kept.
  Kind: feature.
  Source: user-request-2026-10-15.
  Lanes: carry, wizard, worker.
  Blocked-by: GRND-0078.

## Backlog — no version yet

Items not yet placed in a release. Each moves into a version section once it is
scheduled.

- ✅ [GRND-0031] **RPM and OBS packaging.**
  Serves S2. After 0.1.0 (ADR-0002 keeps only the AppImage in 0.1.0).
  User request (2026-10-13): publish Groundwork on OBS as a
  subproject of home:milnet (as home:milnet:ants-terminal is), building
  for all three openSUSE flavours: Tumbleweed, Slowroll and Leap 16.
  Include the OBS publish in every release from now on. Ants Terminal's
  session already runs an OBS subproject; ask it for its spec file,
  _service and osc steps before building ours. Note: home:milnet and
  home:milnet:ants-terminal build no Slowroll tree today (404 checked
  2026-10-13), so the Slowroll repository must be added here.
  Progress (2026-10-13, 9d437fc): spec at packaging/opensuse/,
  scripts at packaging/obs/. home:milnet:groundwork exists (obs-setup.sh);
  OBS revision 2 (v0.3.0 source) built green on Tumbleweed, Slowroll and
  Leap 16.0, ctest 44/44 on each. Decisions: no staging project (a broken
  OBS build after a tag means a patch release); spec Version: and
  _service tag move through .claude/bump.json, and obs-submit.sh refuses
  them out of step; .changes lives on OBS only (osc vc), so no email in
  git. Left: (1) check the published repo answers at
  download.opensuse.org/repositories/home:/milnet:/groundwork/ (404 just
  after the build, publishing pending) and that rpmlint's
  branding-requires-unversioned filter took; (2) README Install gains the
  zypper addrepo route, and docs/ say how releases run the OBS step
  (after the tag is pushed: obs-submit.sh then obs-status.sh); (3) cut
  0.3.1 with cut-release, then run the OBS step, so the Slowroll fix
  ships everywhere.
  Resolved (2026-10-15, 0.3.1): the published repository answers on
  all three targets, and rpmlint's filter took (0 errors). README Install
  offers zypper addrepo per system (858889f); .claude/bump.json names the
  OBS step and checks osc in publish_precheck. 0.3.1 tagged and released
  with the AppImage; OBS revision 3 (v0.3.1) built green on Tumbleweed,
  Slowroll and Leap 16.0, ctest 44/44 on each (obs-status.sh exit 0).
  **Layman:** Makes the app installable from openSUSE's own software sources.
  Kind: package.
  Source: brief-2026-09-25.
  Lanes: packaging.
  Blocked-by: GRND-0012.

- ✅ [GRND-0080] **tst_wizard's OneUp test fails on a machine with OneUp installed.**
  offersOneUpOnlyWhenItIsInstalled sets PATH to /usr/bin:/bin for its
  "no oneup" half, but OneUp installs /usr/bin/oneup, so the button shows
  and the test fails there (seen 2026-10-15 on the code before GRND-0077).
  CI passes only because its runner has no OneUp. Fix: a PATH holding no
  oneup at all.
  Resolved (2026-10-15): the no-OneUp half now runs with an empty folder
  as PATH. Failed three times on the old test here, passes now.
  **Layman:** A check in Groundwork's tests wrongly fails on computers that already have OneUp.
  Kind: fix.
  Source: in-session-2026-10-15.
  Lanes: tests.

- 📋 [GRND-0081] **Item: developer tools for Claude Code and Ants Terminal, tree-sitter first.**
  User request (2026-10-16): offer developer tools, including the code
  tracer whose name was "tree something", as a step a non-developer can
  skip. It joins GRND-0027's developer tools group on the Nice to have
  page, where each item is its own switch and starts off unless chosen.
  Packages on Tumbleweed (zypper search, 2026-10-16): tree-sitter (the
  command), libtree-sitter0_27, and one tree-sitter-<language> package
  per language (tree-sitter-c, -cpp, -python, -bash, -rust, -lua, -json
  among them). Before building: pick the language set; check each
  package exists on Leap 16 and Slowroll, and leave out what does not.
  Widened (user, 2026-10-16): Ants Terminal, and through it Claude
  Code, will use tree-sitter, so it needs installing; the item also
  installs any other tool that helps Claude Code, in particular by
  cutting its token use. This conflicts with claude-config's message the
  same day (ANTS-5734 to vendor its grammars, no system packages); asked
  ants-terminal the same day for the packages it needs or prefers, which
  languages, and which are required. Build from its answer.
  **Layman:** A switch among the developer tools that installs what Claude Code and Ants Terminal use to read code, so they work better and cheaper; anyone who is not a developer leaves it off.
  Kind: feature.
  Source: user-request-2026-10-16.
  Lanes: items.
