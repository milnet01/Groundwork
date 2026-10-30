<!-- ants-spec-format: 1 -->
# GRND-0073 — Carry system-wide settings in a saved setup

**Status:** accepted (2026-10-30). No review gate: this project cancels it (CLAUDE.md, Reviews and specs).
**Kind:** feature.
**Source:** ROADMAP GRND-0073 (user request, 2026-10-12; scope 2026-10-30; design choices 2026-10-30).
**Pairs with:** GRND-0078, GRND-0079, whose clauses §11 changes.

**Layman:** A saved setup can also bring back your firewall rules, printers, extra drives and shared folders, not just your own settings.

## 1. Goal

The carry job offers four more kinds of setting: the firewall, printers, drive mounts and network shares. Pressing Save asks for the password once, and only when the firewall or printers are switched on. On the new install, each kept kind is restored with a switch, and anything it replaces is kept beside it as a numbered copy.

## 2. Problem

1. The saved file covers programs, sources, items and the user's own settings (`Manifest`, `src/carry/carry.h`). Nothing outside the home folder but those.
2. Two of the four cannot be read as the user. `/etc/firewalld` is mode 750 and `/etc/cups/printers.conf` mode 600 (`stat`, 2026-10-30). Item checks never run as root (CLAUDE.md), and `Worker::saveSetup` has only `needsRoot = false` steps.
3. This is a trust boundary. The file may have been edited or damaged on a stick (GRND-0078 §2), and restoring writes to `/etc` as root. Every earlier part of the file went only to the home folder, as the user, or through validated names to `zypper`.
4. `/etc/fstab` also holds the installer's own lines: 13 entries on the owner's machine, 10 of them system (`/`, subvolumes, swap, EFI; `grep` on 2026-10-30). Carrying one could stop the new install booting.
5. `smb.conf` can run commands as root (`root preexec`), and a share may be password-protected. Restoring its sections as written would be root code from a file, and could open a private share to anyone.

## 3. Scope decisions (agreed with the user)

**The user's, 2026-10-30:**

- All four kinds: firewall, printers, drive mounts, network shares. Claude recommended the first three; the user added shares.
- Saving asks for the password once, and only when a kind needing root is switched on.

**Earlier (user, 2026-10-30, in the roadmap item):** programs, sources, items and the user's own settings stay with GRND-0078 and GRND-0079; item checks never run as root.

**Claude's, following from those. The user may overrule any of them:**

- Printers start off and warn, like SSH keys (GRND-0078 §4.4). A printer's address can hold a print server's password.
- The firewall starts on, so most Saves now ask for the password. The row says so.
- Mounts cover local drives only. A network mount can hold a password in its options (§9).
- Only shares with no password are carried, since Groundwork's share item makes every share open to guests (GRND-0066). Others are named as not carried.
- Everything kept starts on when restoring, as other carried parts do (GRND-0079).

## 4. Design

### 4.1 The saved file stays format 1

An older Groundwork refuses a file with a member its list does not name (`membersProblem`), or a format above 1. So the firewall and printers are ordinary `settings` rows, under paths nothing else uses:

```json
"settings": [
  { "name": "Firewall", "paths": [".groundwork-system/firewall"], "secret": false, "bytes": 2048 },
  { "name": "Printers", "paths": [".groundwork-system/printers"], "secret": true,  "bytes": 9000 }
],
"mounts": [
  { "source": "UUID=11111111-2222-3333-4444-555555555555", "point": "/mnt/Games",
    "fstype": "auto", "options": "nosuid,nodev,nofail" }
],
"shares": [ { "path": "/mnt/Games/PC Games", "write": false } ]
```

An older reader ignores `mounts` and `shares` (GRND-0078 §4.7, unknown keys), and restores the two rows into `~/.groundwork-system/`, which does nothing. Removing secrets (`secretMembers`, `withoutSecrets`, GRND-0078 §4.8) needs no change, since Printers is a secret row.

This version's reader treats those two paths apart: `readManifest` moves them out of `Manifest::settings` into `Manifest::systemParts`, and forces Printers' `secret` true whatever the file says. `gatherSettings` never offers `.groundwork-system`.

Member names measured 2026-10-30 with GNU tar 1.35 in a scratch tree, including the `--transform` and `--concatenate` below: `tar --list --verbose` gives each member's type as the first character, and a restore by name with `--strip-components=2` kept `home.xml.~1~` beside the new file.

### 4.2 Gathering, with no password

`gatherSystem(context, into)`, called from the existing gather callback. It never runs a command that can raise a password box (`firewall-cmd` is not used; project memory, 2026-10-10).

| Kind | Offered when | Row shows | Starts |
|---|---|---|---|
| Firewall | `readService` finds `firewalld` | "Firewall rules. Save will ask for your password." | on |
| Printers | `lpstat -e` lists a printer | The names, each through `isolated()`, and the secret warning when switched on | off |
| Mounts | §4.3 keeps a line | One row per line, naming its mount point | on |
| Shares | §4.4 keeps a section | One row per folder, naming look-only or add-files | on |

The Save page's size check adds `kSystemPartAllowance`, 16 MiB, per root-read kind switched on, since its size cannot be read before the password.

### 4.3 Which mounts

`carriedMounts(fstabText)`, a pure function over `/etc/fstab`, which any user can read. A line is kept only when all of these hold:

- Its source is `UUID=`, `LABEL=`, `/dev/disk/by-uuid/` or `/dev/disk/by-label/`, with no space or control character.
- Its mount point is absolute, equal to its own `QDir::cleanPath`, and not `/` or under `/bin`, `/boot`, `/dev`, `/etc`, `/home`, `/lib`, `/lib64`, `/opt`, `/proc`, `/root`, `/run`, `/sbin`, `/srv`, `/sys`, `/tmp`, `/usr` or `/var`.
- Its filesystem is not `btrfs`-with-`subvol=`, `swap`, `vfat` at `/boot/efi`, or a network type (`nfs`, `nfs4`, `cifs`, `smb3`, `sshfs`, `fuse.sshfs`).
- Its options match the pattern below, and hold no `password`, `credentials` or `user=`.

```regex pcre2
^[A-Za-z0-9_,=.:+-]*$
```

| input | expected |
|---|---|
| `nosuid,nodev,nofail,noatime,x-gvfs-show` | `nosuid,nodev,nofail,noatime,x-gvfs-show` |
| `defaults,uid=1000` | `defaults,uid=1000` |
| `a b` | no match |
| `defaults;x` | no match |

`readManifest` applies the same function's rules to `mounts`, so a file cannot carry what gathering refuses.

### 4.4 Which shares

`carriedShares(smbConfText, currentValue)`, a pure function. It takes every folder in `NetworkSharesItem`'s `currentValue`, and every other section of `smb.conf` with `guest ok = yes` and a `path`. `write` is true when that section has `read only = no` or `writable = yes`. Every other setting in the section is dropped, so `root preexec` and the like cannot travel. A section without `guest ok = yes`, or whose path `NetworkSharesItem::isValidValue` refuses, is not carried; the row says which and why. Reading `smb.conf` needs no root (mode 644, `stat`, 2026-10-30).

### 4.5 Saving with the password

`CarryChoice` records `systemKinds`. `Worker::saveSetup` gains these steps, after the archive is written (GRND-0078 §4.5 step 3) and before it is listed. Root is asked for once, by `ensureRoot`, and only when a kind needs it. Refusing it fails the run: "Nothing was saved."

Firewall:

```
tar --create --format=posix --file <state>/system.tar -C /etc/firewalld \
    --transform 's,^\./\?,.groundwork-system/firewall/,' .
```

Printers:

```
tar --create --format=posix --ignore-failed-read --file <state>/system.tar -C /etc/cups \
    --transform 's,^,.groundwork-system/printers/,' printers.conf classes.conf ppd
```

`--ignore-failed-read` because `classes.conf` may not exist; without it, a missing name exits 2 (measured 2026-10-30). Each is `needsRoot = true`, followed by these:

```
chown <uid>:<gid> <state>/system.tar      (root)
tar --concatenate --file <output>.partial <state>/system.tar
rm -f -- <state>/system.tar
```

`cupsd.conf`, `ssl/` and `subscriptions.conf` are never read. The Worker adds the row to the manifest before writing it. The manifest and the archive agree, or `membersProblem` fails the run (GRND-0078 INV-5).

### 4.6 Restoring

`carriedItems` builds, after `carried-settings`:

| Id | Check, never as root | Apply |
|---|---|---|
| `carried-firewall` | `NotDone` when `firewalld` is found, since the rules cannot be read. `CouldNotTell` when `readService` did not answer | §4.7 |
| `carried-printers` | As firewall, with `cups`. When `cups` is not found, `NotDone`, and its apply installs it | §4.8 |
| `carried-mounts` | `Done` when every kept mount whose drive is present has a line in `/etc/fstab`; the detail names absent drives | §4.9 |

`carried-firewall` depends on `firewall`. `carried-printers` depends on `system-update` only while `cups` is missing, as GRND-0111 arranged for others. Shares build no item: the page switches on `network-shares` and fills its `FolderListField` with the kept folders, joined with those it already holds. Its own row, its warning (GRND-0066 INV-10) and its validation apply unchanged.

Each carried kind's row says it changes system settings. The firewall and printers always read `NotDone`, so applying them twice is possible. That is safe, because each apply keeps numbered copies. These items exist only on a run from a saved file, so S1 is untouched.

Before any root step, `systemMembers(file, kind)` reads `tar --list` and `tar --list --verbose` through `CheckContext::run`, same order line by line. Every member under `.groundwork-system/<kind>/` must be a folder or a regular file, and its name must match. Any other member refuses that whole item before a step runs, naming the member through `isolated()`.

```regex pcre2
^\.groundwork-system/firewall/(?:firewalld\.conf|(?:zones|services|policies|ipsets)/[A-Za-z0-9_.+-]+\.xml)$
```

| input | expected |
|---|---|
| `.groundwork-system/firewall/zones/home.xml` | `.groundwork-system/firewall/zones/home.xml` |
| `.groundwork-system/firewall/zones/../../sudoers` | no match |
| `.groundwork-system/firewall/lockdown-whitelist.xml` | no match |

```regex pcre2
^\.groundwork-system/printers/(?:printers\.conf|classes\.conf|ppd/[A-Za-z0-9_.+-]+\.ppd)$
```

| input | expected |
|---|---|
| `.groundwork-system/printers/ppd/Office.ppd` | `.groundwork-system/printers/ppd/Office.ppd` |
| `.groundwork-system/printers/cupsd.conf` | no match |

A folder member is not extracted; only named files are, and the folders they sit in are made by `mkdir -p` from a fixed list. Files other than those matching are simply refused rather than skipped, so a tampered file changes nothing.

### 4.7 The firewall

`firewalld.conf` holds settings a later version may read differently, so it is never written. Its `DefaultZone` is read through `CheckContext::run` (`tar -xOf`), and must match `^[A-Za-z0-9_-]+$`. All steps root:

```
mkdir -p -- /etc/firewalld/zones /etc/firewalld/services /etc/firewalld/policies /etc/firewalld/ipsets
tar --extract --file FILE -C /etc/firewalld --strip-components=2 --backup=numbered --no-same-owner
    --no-overwrite-dir --verbatim-files-from --null --files-from=-      (the named .xml files on input)
restorecon -R /etc/firewalld                                            (when SELinux is on; see §15)
firewall-cmd --reload
firewall-cmd --set-default-zone=ZONE                                    (when DefaultZone is valid)
```

### 4.8 Printers

`cupsd` writes `printers.conf` when it stops, so it must not be running while the file is replaced (§15). All steps root:

```
zypper -n install cups                     (when not found)
systemctl stop cups
mkdir -p -- /etc/cups/ppd
tar --extract … -C /etc/cups --strip-components=2 --backup=numbered --no-same-owner …
chown root:lp /etc/cups/printers.conf      (and classes.conf when kept)
chmod 600 /etc/cups/printers.conf
restorecon -R /etc/cups                    (when SELinux is on; see §15)
systemctl enable --now cups
```

Printer drivers come from the carried programs, which run first.

### 4.9 Mounts

`missingMounts` compares the kept mounts with `/etc/fstab` and with `/dev/disk/by-uuid` and `/dev/disk/by-label`. A kept mount is left out, and named in a `HINT`, when its drive is not present, or `/etc/fstab` already has its mount point or its source. The rest go exactly as `ExtraDrivesItem::applySteps` does: `makeDirectoriesStep`, an append by `dd oflag=append conv=notrunc`, `systemctl daemon-reload`, then `mount` for each. `nofail` is added to any line without it, so a drive removed later cannot stop a start-up. Unlike that item, it never changes a drive's owner: these drives hold files.

## 5. Invariants

- **INV-1** — No item check runs as root, and none of the four kinds' gather or check steps runs `firewall-cmd` or `sudo`. *Test:* `tests/tst_carrysystem.cpp::nothingAsksForAPasswordBeforeSave`, recording every command through gather and check with an unreadable `/etc/firewalld`. *Breaks when:* a check reads root-only files, which would read "couldn't tell" and skip the item forever (CLAUDE.md).

- **INV-2** — Saving asks for root exactly when the firewall or printers are switched on. *Test:* `tests/tst_worker.cpp::saveAsksForRootOnlyWhenNeeded`, with each combination and a fake `sudo` counting calls. *Breaks when:* mounts alone ask, or a root step runs per kind beyond the one `ensureRoot`.

- **INV-3** — A file holding the firewall or printers still reads in a Groundwork of this format without error, and its parts are named by its manifest. *Test:* `tests/tst_carrysystem.cpp::aFileWithSystemPartsReadsBack`, through `readSetupFile` and `membersProblem`. *Breaks when:* the row and the member names disagree, or the format number moves.

- **INV-4** — Only §4.6's names reach a root `tar`: a link, a device, a `..`, or a name outside the pattern refuses the item before any step. *Test:* `tests/tst_carrysystem.cpp::aTamperedMemberRefusesTheItem`, a data table of archives with a link at `zones/home.xml`, `zones/../../sudoers`, `cupsd.conf` and a device. *Breaks when:* one of them produces a step, or a root `tar` runs without `--files-from`.

- **INV-5** — `/etc/fstab`'s system lines are never carried: `/`, any subvolume, swap, EFI, anything under a §4.3 folder, and any line with a secret option. *Test:* `tests/tst_carrysystem.cpp::systemMountsAreNotCarried`, on a fixture copying a default Tumbleweed `fstab`, and through `readManifest` for a file naming `/`. *Breaks when:* a `/home` line is kept, or `readManifest` trusts the file's list.

- **INV-6** — A kept mount whose drive is absent, or whose point or source is already in `fstab`, adds no line; and any line added has `nofail`. *Test:* `tests/tst_carrysystem.cpp::onlyPresentDrivesAreAdded`. *Breaks when:* an absent drive is written, or a second apply adds a second line.

- **INV-7** — A share reaches the new install only through `network-shares`, and only as a path and a look-only or add-files flag: no other line of the old `smb.conf` travels, and a section without `guest ok = yes` does not travel at all. *Test:* `tests/tst_carrysystem.cpp::sharesCarryNothingElse`, a section with `root preexec` and one with `valid users`. *Breaks when:* a setting beyond path and write appears in the manifest, or a password-protected share is listed.

- **INV-8** — Printers are a secret row whatever the file says: start off when saving, marked when restoring, removed by remove-secrets. *Test:* `tests/tst_carrysystem.cpp::printersAreAlwaysSecret`, with `"secret": false` in a crafted file, then `secretMembers`. *Breaks when:* the reader trusts the file's flag.

- **INV-9** — A replaced system file is kept as a numbered copy, and `firewalld.conf` is never written. *Test:* `tests/tst_carrysystem.cpp::restoreKeepsCopies`, on the steps' arguments. *Breaks when:* `--backup=numbered` is dropped, or `firewalld.conf` reaches the extract list.

- **INV-10** — Printers are restored with `cups` stopped: stop comes before the extract, start after it. *Test:* `tests/tst_carrysystem.cpp::printerStepsStopCups`. *Breaks when:* the order changes, and `cupsd` overwrites the restored file.

- **INV-11** — On real systems, a saved firewall, printer, mount and share work on a fresh install. *Test:* §7's manual recipe. *Breaks when:* firewalld, CUPS or SELinux behave other than §4.7 and §4.8 say.

## 6. Failure modes

- **The password is refused at Save.** Nothing is saved, and the page says so. The user can switch those rows off and save again.
- **A tampered file names an allowed path with hostile content.** A zone file could open every port; a PPD names filters, which CUPS runs only from its own folder. Accepted: the user chose the file, the rows say they change system settings, and nothing runs from it. Hostile names and types are INV-4.
- **A carried zone names a service the new install lacks.** firewalld refuses it at `--reload`, the step fails, and the numbered copies remain for repair.
- **`firewalld` was not running.** `carried-firewall` depends on `firewall`, which starts it. If that failed, the item is skipped, as for any dependency.
- **A printer's driver is missing.** The printer appears and its jobs fail. Its package comes from the carried programs if it was kept, and is otherwise for the user to install.
- **A printer is unplugged or its server is away.** CUPS keeps it listed, as it would on the old machine.
- **A drive's mount point folder holds files.** `mount` covers them until unmounted, and they are not lost. As `ExtraDrivesItem` does.
- **A mount's drive is present but its filesystem driver is not.** The `mount` step fails and the item says so. `nofail` keeps start-up safe.
- **A shared folder is gone.** As GRND-0066 §6: the share is written, and Samba refuses it until the disk returns.
- **The saved file is older than this version.** No system parts and no new keys, so no new rows.
- **The saved file was opened by an older Groundwork.** Two inert folders appear under `~/.groundwork-system/`. Mounts and shares are ignored.

## 7. Tests

Each new test must be seen to fail before its code exists.

| Invariant | Test |
|---|---|
| INV-1, INV-3, INV-4, INV-5, INV-6, INV-7, INV-8, INV-9, INV-10 | `tests/tst_carrysystem.cpp`, a new target in `tests/CMakeLists.txt`, with `FileReader` and fake runner fixtures |
| INV-2 | `tests/tst_worker.cpp::saveAsksForRootOnlyWhenNeeded` |
| INV-11 | the recipe below |

These tests prove the commands Groundwork builds. Only the recipe proves firewalld, CUPS and SELinux take the result.

### Manual recipe (INV-11), in two Tumbleweed VMs before release, SELinux enforcing

1. On the first VM: open a firewall service with `firewall-cmd --permanent --add-service=ftp`, change the default zone, add a printer (the `ipp://` form to a fake address is enough), add a second disk with a `UUID=` line under `/mnt`, and share a folder through `network-shares`.
2. In the carry job, the page raises no password box. Switch printers on. Save asks once. `tar --list` of the file shows both parts, and no `cupsd.conf`.
3. On a fresh second VM with that disk attached, start from the file. Rows appear for each kind, printers marked secret.
4. After the run, with no manual repair: `firewall-cmd --list-services` includes `ftp`; the default zone matches; `lpstat -e` lists the printer; `findmnt /mnt/<name>` shows the disk, and `/etc/fstab` has `nofail`; `smbclient -N -L` lists the share.
5. `ausearch -m avc -ts recent` shows no denial for firewalld or cupsd. `ls -Z` on a restored file shows the type its folder's others have.
6. `systemctl restart firewalld cups`, then repeat step 4's first three checks. Restart the VM, and again.
7. Run from the same file a second time: numbered copies appear and `fstab` gains no line.
8. Detach the disk and repeat on a third fresh VM: the mount is named as absent, and the VM starts normally.
9. Repeat steps 3 and 4 on Leap 16.

## 8. Alternatives considered (and rejected)

- **Asking for the password while gathering.** The page would raise a box before the user has chosen anything, and one who wants no root gets one regardless.
- **Item checks reading through `sudo`.** CLAUDE.md, Item checks never run as root.
- **A format 2 file.** Every older Groundwork would refuse the whole file for parts it could otherwise ignore.
- **An archive nested inside the file.** Measured working, but reading its member list needs unpacking it first, so validation comes later and needs a stage folder. Flat members are read by the existing `CheckContext::run`.
- **Unnamed members under `.groundwork-system`.** `membersProblem` in older versions refuses them.
- **Restoring `firewalld.conf` whole.** Its keys change with versions; only the default zone is the user's.
- **Replacing `/etc/fstab`, or carrying every line.** §2.4.
- **Restoring `smb.conf` sections as written.** §2.5.
- **`cupsctl` or `lpadmin` per printer.** It rebuilds from a URI and a driver, and loses settings such as paper size and any PPD changes, but may suit better if the recipe shows the file route fails.

## 9. Out of scope

- Network mounts, with their passwords — deferred; not yet queued.
- Wi-Fi and other network connections, which hold passwords in `/etc/NetworkManager/system-connections` — deferred; not yet queued.
- Other `/etc` settings, such as `sysctl.d`, `udev` rules or `modprobe.d` — deferred; not yet queued.
- Comparing the restored firewall with what was saved, for a real "done" — deferred; not yet queued.
- Password-protected shares — deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/tst_carrysystem.cpp::nothingAsksForAPasswordBeforeSave` |
| INV-2 | `tests/tst_worker.cpp::saveAsksForRootOnlyWhenNeeded` |
| INV-3 | `tests/tst_carrysystem.cpp::aFileWithSystemPartsReadsBack` |
| INV-4 | `tests/tst_carrysystem.cpp::aTamperedMemberRefusesTheItem` |
| INV-5 | `tests/tst_carrysystem.cpp::systemMountsAreNotCarried` |
| INV-6 | `tests/tst_carrysystem.cpp::onlyPresentDrivesAreAdded` |
| INV-7 | `tests/tst_carrysystem.cpp::sharesCarryNothingElse` |
| INV-8 | `tests/tst_carrysystem.cpp::printersAreAlwaysSecret` |
| INV-9 | `tests/tst_carrysystem.cpp::restoreKeepsCopies` |
| INV-10 | `tests/tst_carrysystem.cpp::printerStepsStopCups` |
| INV-11 | **nothing** automatic — §7's manual recipe, run in VMs before release |
| §6 an older Groundwork reads a new file | **`Partial:`** INV-3 tests this version's reader. **Nothing** runs an older build against a new file |
| §6 a hostile zone or PPD's content | **nothing** — accepted; the user chose the file |

## 11. Cross-doc impact

- `docs/specs/GRND-0078-saved-setup-file.md`: §4.5's "every step is `needsRoot = false`", §4.6's manifest, §4.7's reader. Amended with this spec, to point here.
- `docs/specs/GRND-0079-start-from-saved-setup.md`: §4.2's table of carried items. Amended with this spec, to point here.
- `docs/specs/GRND-0066-network-shares.md`: §9's "Carrying the share list" now names this item.
- `docs/design.md`, Carrying a setup over: "all without root" and the kinds. Done with this spec.
- `docs/discovery.md`, S6: amended to name system settings. Done with this spec.
- `docs/reference/worker-interface.md`: the root step in `--save-setup`.
- `README.md`, `CHANGELOG.md`, on shipping.
- `translations/*.ts`: new text, through `update_translations`.
- `CLAUDE.md`'s position lines, as the state moves.

## 12. Cold-eyes loop log

Rows live in `../reviews/GRND-0073-system-settings-loop-log.md`.

## 13. Resource cost

`kSystemPartAllowance` is added to the Save page's size check for each root-read kind. One new test target. `Manifest` gains two capped lists, inside the existing `kManifestCap`. No new build dependency.

## 14. Migration / compatibility

A format 1 file saved by an earlier version has no system parts and reads as before. A file saved by this version reads in an older Groundwork, as §4.1 and §6 say. `.groundwork-system` is a new name under the home folder only on an older reader.

## 15. Open questions

Each needs root or a fresh VM, so none is measured here. §7's recipe settles them all before release, and a wrong answer changes only the step it names.

- **Does a restored file need `restorecon`?** Files made in an existing folder usually take the folder's type. The step stays if the recipe's step 5 shows a wrong type, and is dropped if not.
- **Does `cupsd` overwrite `printers.conf` on stop?** Taken from CUPS's known behaviour, unmeasured here. INV-10 is cheap either way.
- **Does `firewall-cmd --reload` take new zone files, and the default zone set after it?** Taken from firewalld's documented behaviour, unmeasured here.
- **Does Leap 16's tar list members the same way?** Measured on Tumbleweed only. The recipe's step 9 reads it.
