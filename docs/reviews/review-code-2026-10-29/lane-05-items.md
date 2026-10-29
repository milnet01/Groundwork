Line counts as I read them with `Read`:

| File | Lines | File | Lines |
|---|---|---|---|
| claudecodeitem.h | 26 | claudecodeitem.cpp | 72 |
| claude-code.asc | 29 | catalogue.h | 12 |
| catalogue.cpp | 152 | amdvideoitem.cpp / .h | 58 / 28 |
| antsterminalitem.cpp / .h | 107 / 28 | appitems.cpp / .h | 64 / 67 |
| bootmenuitem.cpp / .h | 126 / 27 | broadcomitem.cpp / .h | 59 / 30 |
| clockitem.cpp / .h | 76 / 30 | codecsitem.cpp / .h | 67 / 30 |
| diskscheduleritem.cpp / .h | 71 / 24 | firewallitem.cpp / .h | 44 / 22 |
| firmwareitem.cpp / .h | 62 / 23 | flathubitem.cpp / .h | 47 / 22 |
| fontsitem.cpp / .h | 43 / 27 | freezerecorderitem.cpp / .h | 91 / 26 |
| freeze-recorder.sh | 114 | gamingextrasitem.cpp / .h | 44 / 26 |
| hardware.cpp / .h | 29 / 27 | hostnameitem.cpp / .h | 47 / 28 |
| logcapitem.cpp / .h | 95 / 23 | memoryitem.cpp / .h | 126 / 30 |
| numlockitem.cpp / .h | 72 / 22 | nvidiaitem.cpp / .h | 102 / 38 |
| oneupitem.cpp / .h | 75 / 25 | packman.cpp / .h | 87 / 25 |
| poweritem.cpp / .h | 52 / 30 | service.cpp / .h | 20 / 21 |
| snapshotsitem.cpp / .h | 53 / 24 | soundfirmwareitem.cpp / .h | 54 / 29 |
| sshitem.cpp / .h | 52 / 23 | swappinessitem.cpp / .h | 44 / 23 |
| sysrqitem.cpp / .h | 52 / 24 | updateitem.cpp / .h | 58 / 27 |
| virtualmachinesitem.cpp / .h | 56 / 25 | wakeitem.cpp / .h | 108 / 24 |

**Already in my context when I arrived:** the global `~/.claude/CLAUDE.md`, the review-lane agent definition, a SessionStart hook note about the Ants MCP verbs, and the user's email address. There was no project `CLAUDE.md`, no memory index and no git snapshot.

**One disagreement between my brief and the files, and how I settled it.** The shared context says "the files above are provided in full; do not re-read them", but it gave only a pointer to `docs/design.md`, not its text. My tail names three of its sections, so I read those (§ The levels, § Which systems it runs on, § What every part does the same way) and nothing else of it.

**What I read beyond the subject, and why:**
- `src/core/item.h` in full, for the interface.
- Slices of `selection.cpp` (lines 1–60) and `worker.cpp` (lines 95–175), for how dependencies close and how `namesFrom` is used.
- ROADMAP GRND-0082's body, because the new item's header cites it.
- CHANGELOG lines 39–44, which a search returned.
- One search of `src/carry` for the dimension 2b caller count. It found that `namesFrom` is set at `carrieditems.cpp:132`.
- The Idioms sections of `cpp.md` and `qt.md`.

I opened nothing under `tests/`. `docs/research/2026-10-02-sources.md` has no Claude or Anthropic entry: a search of `docs/research` found none, and I did not read the file further.

## Critical (0)

## High (0)

## Medium (2)

- [dim 2] `firewallitem.cpp:37` — `steps.append({{QStringLiteral("zypper"), QStringLiteral("-n"), QStringLiteral("install"), kUnit},`
  - **The problem:** five items install packages without declaring `installsPackages()`, so they never depend on the update preparation. They are the firewall (`firewalld`), firmware (`fwupd`, `firmwareitem.cpp:52`), memory (`systemd-experimental`, `memoryitem.cpp:90-91`), snapshots (`snapper`, `snapshotsitem.cpp:44-45`) and SSH (`openssh-server`, `sshitem.cpp:35-36`).
  - **What the contract says:** design § The levels: "Every item that installs packages depends on it, so the installs meet a current system." `Catalogue::dependencies` (`selection.cpp:16`) adds the update preparation only when `installsPackages()` is true.
  - **A concrete failure:** `systemd-experimental` must match the installed systemd's exact version. On a Tumbleweed that is behind, installing it alone drags in a partial systemd upgrade, without the full update the design promises before installs.
  - **Which side is wrong:** the code.
  - **Fix:** return `true` from `installsPackages()` on those five items.
- [dim 2] `appitems.cpp:16-17` — `if (!r.started) return {CheckState::NotDone, ... "Flatpak is not installed."};`
  - **The problem:** when Flatpak is missing, the check returns before the `rpm -q` loop over `m_native`. An app installed the ordinary way therefore reads "not done", and switching it on installs a second copy from Flathub.
  - **What the contract says:** `appitems.h:8-9` promises an app installed that way "is not offered twice".
  - **Fix:** run the native-package loop before returning on `!r.started` or a timeout.

## Low / Info

- [dim 16] `claudecodeitem.cpp:58-63` — `QString alias = repositoryAlias(context, kRepoPath); if (alias.isEmpty()) { alias = kPackage; steps.append({... "addrepo" ...`
  - **The problem:** `repositoryAlias` matches only enabled repositories. If a disabled repository named `claude-code` already exists, `zypper addrepo` fails because the alias is taken, and it fails the same way on every run. OneUp (`oneupitem.cpp:60-65`) and Ants Terminal (`antsterminalitem.cpp:82-87`) share the pattern. LOW.
  - **Fix:** look up the alias whether or not the repository is enabled, and enable a disabled match (`zypper modifyrepo -e`) instead of adding it again.
- [dim 7] `claudecodeitem.cpp:27` — `return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();`
  - **The problem:** if the built-in key cannot be read, an empty key file is written and `rpm --import` fails with only "Installing Claude Code failed (exit code N)". The cause is lost.
  - The same applies to `freezerecorderitem.cpp:53`, which would install an empty script under a `Restart=always` service that restarts it every 5 seconds.
  - **Why LOW:** both files are compiled into the program (`CMakeLists.txt:95`), so the path is close to unreachable.
  - **Fix:** if either file is empty, return a single step that fails with a message naming the missing file.
- [dim 2] `logcapitem.cpp:88-89` — `"[Journal]\nSystemMaxUse=1G\nSystemKeepFree=2G\nSystemMaxFileSize=128M\n" "MaxRetentionSec=3month\n"`
  - **The problem:** the file also deletes log entries older than three months, and keeps 2 GB of the drive free. Neither the item's sentence nor design § The levels ("A 1 GB limit on the system log, which deletes the oldest entries first") mentions this. It is undocumented behaviour at a point the documents address.
  - **Which side is wrong:** I cannot tell. Either the code or the documents.
  - **Fix:** drop the two extra lines, or say in the sentence and the design what they do.
- [dim 2] `bootmenuitem.cpp:67` and `:120` — the check finds `line.trimmed().startsWith("GRUB_TIMEOUT=")`, but the apply runs `s/^GRUB_TIMEOUT=.*/`.
  - **The problem:** on an indented `GRUB_TIMEOUT` line, `sed` changes nothing and still exits 0. The step reports success, and the item stays "not done".
  - **Fix:** anchor the `sed` expression on `^\s*GRUB_TIMEOUT=`.
- **INFO (my coverage):** I decoded the issuer-fingerprint subpacket of the self-signature in `claude-code.asc`. It reads `31DDDE24DDFAB679F42D7BD2BAA929FF1A7ECACE`, which matches `claudecodeitem.cpp:23` and the roadmap. The block also has no expiry subpacket.
  - **What I could not do:** compute the key's real v4 fingerprint (a SHA-1 hash of the key packet), which is what rpm checks. So I matched what the key claims about itself, not the key.
  - **The check to run:** `gpg --show-keys src/items/claude-code.asc`.
- **INFO:** I ran none of the listed tools. Nothing above carries a `[tool:]` tag, but shellcheck would likely flag SC2015 at `freeze-recorder.sh:31` (`[ -r "$f" ] && echo … || echo "?"`). It is harmless there.
- **Dimensions given one line each:**
  - dim 3: no shell anywhere. Every value goes in argv, and the hostname is validated (`hostnameitem.cpp:25`). The built-in key avoids trusting a downloaded key (no `--gpg-auto-import-keys` on the Claude Code refresh). Nothing else found.
  - dim 2b: the promised entry points have callers. `alsoOfferedBy` is consumed at `wizard.cpp:678-681`, and `namesFrom` is set at `carrieditems.cpp:132`. Nothing found.
  - dim 15: nothing in this lane sends user data off the machine. The only network traffic is the downloads themselves. Nothing found.
  - dims 4, 5, 8, 9, 10, 11, 12, 13, 17: weighted out. I scanned for them and found nothing.

## Covered by spec and looks correct

**ClaudeCodeItem.**
- The check reads only files the user can read: the `PATH` folders, `~/.local/bin` and `/etc/zypp/repos.d`. It therefore cannot fall into the root-only "couldn't tell" trap, and runs no command that could raise a password box.
- The apply steps match the roadmap's measured sequence: make the folder, write the key, `rpm --import`, `addrepo -f`, refresh, install.
- Commands are named bare, as design § What every part does the same way requires, and the strings are translatable.
- A re-run is idempotent: the repository is found and not added again.

**Elsewhere.**
- `catalogue.cpp` lists every item after what it depends on.
- Dependency closure (`selection.cpp:13-22`) gives the Claude Code item its update dependency.
- For the known traps, I checked every item's check for root-only reads and for commands that raise a password box (`systemctl is-*`, `timedatectl show`, `hostnamectl`, `flatpak remotes`, `fwupdmgr get-updates`, and the efivarfs and sysfs reads). All read only what a user can, apart from `snapper` (open question below).
- zypper exit code 106 in the update check matches the design.

## Open questions

- **Does Ants Terminal really offer Claude Code yet?** `claudecodeitem.h:20` makes the row say "Ants Terminal offers this too" whenever Ants Terminal is switched on. The roadmap says that is "true only once that Ants Terminal release ships", and v0.7.114 lacks it. Is it true for what OBS installs today?
- **Does `snapper list-configs` raise a password box?** `snapshotsitem.cpp:29` runs it as the user. The known trap names some snapper reads, and I could not test this one.
- **When are SSH's steps built?** `sshitem.cpp:42` decides at plan time whether firewalld is running. If the steps are built before the firewall item applies in the same run, SSH is never let through the firewall that run just switched on. I did not read when the Worker calls `applySteps`.
- **Should the sentence say the key is trusted for everything?** `rpm --import` makes Anthropic's key trusted for every repository, not only this one, and the Claude Code sentence does not say a key is trusted. The codecs sentence does say so. I found no contract that settles it.
- **Should catalogue installs use `namesFrom` too?** `PackageItem` install steps (for example the 25 tree-sitter packages) never set `namesFrom`, so one clashing name stops the whole install. Is GRND-0086 meant to cover catalogue items, or only carried ones?

## 3 items to fix first

1. **The five items that skip the update dependency** (Medium, dim 2). It is a broken contract on common first-run paths, and `systemd-experimental` can cause a partial Tumbleweed upgrade.
2. **The Flathub apps' check without Flatpak** (Medium, dim 2). It breaks a stated promise and installs a second copy of an app the user already has.
3. **The disabled `claude-code` repository** (Low, dim 16). It is the one failure in the new item that can never recover on a re-run, and the same fix covers OneUp and Ants Terminal.
