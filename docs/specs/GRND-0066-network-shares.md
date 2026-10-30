<!-- ants-spec-format: 1 -->
# GRND-0066 — Share chosen folders on the home network

**Status:** accepted (2026-10-30). No review gate: this project cancels it (CLAUDE.md, Reviews and specs).
**Kind:** feature.
**Source:** ROADMAP GRND-0066 (user request, 2026-10-10; design choices 2026-10-30).

Other computers at home can open the folders you pick in Groundwork, with no password, and you choose for each folder whether they may only look or may also change what is in it.

## 1. Goal

A new Configuration row lets the user build a list of folders and set each one to look-only or add-files. Applying it installs and starts Samba and wsdd, opens the firewall, and publishes those folders to guests. Files guests write belong to the user. Shares the user already has in `smb.conf` keep working, unchanged.

## 2. Problem

1. No item shares a folder. `src/items/catalogue.cpp` has no Samba item.
2. `--set id=value` carries one string (`Wizard::workerArguments`, parsed in `src/main.cpp::main`), and the only value field is one line of text (`ItemRow::valueField`). A list of folders fits neither the field nor the step mechanism: `Worker::run` substitutes a value into `Step::argv` only, and `Item::applySteps` never sees the value, so an item cannot write a file built from it.
3. `Worker::run` skips any item whose re-check is not `NotDone`. Once sharing is on, adding a folder would read "Already done" and change nothing.
4. An item check cannot read the firewall. `/etc/firewalld/zones` and `/etc/firewalld/firewalld.conf` refuse the user (`ls` and `grep` on 2026-10-30: permission denied), and `firewall-cmd` can raise a password box (project memory, 2026-10-10).
5. This boundary is the network: whatever is shared is open to anyone who can reach the machine, so a mistake in what gets written is a security fault, not a cosmetic one.

## 3. Scope decisions (agreed with the user)

**The user's, 2026-10-30 (roadmap item):**

- Anyone at home opens the shares as a guest, with no password. Claude recommended a password and the user chose otherwise. Not reopened here. The row says plainly what this means (INV-10).
- Look-only or add-files is set per folder.
- Folders are picked in Groundwork, from an add-folder list.

**Claude's, following from those. The user may overrule any of them:**

- wsdd is switched on. The roadmap item asks for it "so Windows finds it". Whether Windows finds the computer without it is unverified here; §7's recipe looks.
- Every Groundwork share uses `force user`, look-only ones too. A guest otherwise reads as `nobody`, which cannot enter a folder like `/mnt/Emulators` (mode 700 on the owner's machine).
- Look-only is the starting position for each added folder, so a folder is opened for changes only by moving its switch.
- The switch says "add, change and delete". Samba's `read only = no` allows all three, and calling it "add files" would promise less than it does (§4.5).

## 4. Design

### 4.1 Where the shares live

Groundwork owns one file, `/etc/samba/groundwork-shares.conf`, and rewrites all of it on each apply. `/etc/samba/smb.conf` gains one comment and one line, appended once, and is never otherwise changed:

```
# Added by Groundwork: the folders it shares.
include = /etc/samba/groundwork-shares.conf
```

Samba reads an include as though typed in place. `testparm -s` on 2026-10-30 confirmed the shape below: appended after a last share, the included `[global]` and new section both load, and that last share keeps its settings. An empty included file loads too. `[global]` is repeated because a guest cannot connect without `map to guest`, and an included file may set it.

```
# Written by Groundwork. Change it from Groundwork, not here.
[global]
   map to guest = bad user

[PC Games]
   path = /mnt/Games/PC Games
   browseable = yes
   guest ok = yes
   read only = yes
   force user = ants
   create mask = 0664
   directory mask = 0775
   mangled names = no
```

`force user` is the name for `::getuid()`, read through `getpwuid`, since the Worker runs as the user. The masks and `mangled names` copy the owner's own writable shares. `wide links` is not set: its default, `No` (`testparm -sv` on 2026-10-30), keeps a symlink from leading a guest out of the folder.

### 4.2 The value

The list reaches the Worker through the existing `--set`, as compact JSON in one argument. `QProcess` passes it without a shell, so spaces need no quoting, and `src/main.cpp` splits on the first `=` only, so a path holding `=` survives:

```
--set network-shares=[{"path":"/mnt/Games/PC Games","write":false}]
```

`[]` means "share nothing through Groundwork". An empty string means no list was given, and is invalid.

### 4.3 What a value may hold

`NetworkSharesItem::isValidValue` accepts JSON of the §4.2 shape and refuses the whole value when any entry breaks a rule. The Wizard and the Worker both call it. `Item::whyRefused(path)`, a virtual `NetworkSharesItem` overrides, returns the plain sentence the list shows when a folder is refused.

| Rule | Why |
|---|---|
| Absolute, and equal to its own `QDir::cleanPath` | no `..` or doubled slash |
| No control character, `%`, `"` or `\` | a newline would start a new line in the file; `%` is a Samba substitution; a final `\` continues the line |
| No leading or trailing space, and no two spaces together | Samba trims the ends and squeezes spaces inside (`testparm -s`, 2026-10-30), so another folder would be shared |
| A link's target obeys these rules too | Samba follows a share's own path, whatever `wide links` says |
| Not `/` | the whole machine |
| Not `/bin`, `/boot`, `/dev`, `/etc`, `/lib`, `/lib64`, `/opt`, `/proc`, `/root`, `/sbin`, `/sys`, `/usr`, `/var` or below them | system files |
| Not under `/run`, except `/run/media` | a drive the desktop opened lives there |
| Not the home folder, `/home`, or any folder holding the home folder | keys and passwords would be open |
| Below the home folder: no part of the path starts with `.` | `~/.ssh`, `~/.config` |
| No path listed twice | one folder, one share |
| At most 20 entries, each path at most 1024 bytes | keeps the argument far below Linux's limit on one argument |

### 4.4 Share names

`shareNames(paths, taken)`, a pure function: the folder's last part, each of ``[ ] " / \ : | < > + = ; , ? * %`` and each control character replaced by `_`, spaces and dots trimmed, cut to 64 characters, and `Folder` if nothing is left. Comparing without regard to case, a name already taken gets ` 2`, ` 3` and so on.

`taken` holds `global`, `homes`, `printers`, `print$`, `ipc$`, `profiles`, `users`, `groups`, `netlogon`, the sections of `smb.conf` as read now, and the names before it in the list. Two sections with one name would merge, so a Groundwork share could change a share of the user's. The fixed names are there because when Samba is not yet installed, there is no `smb.conf` to read until the package puts one there.

### 4.5 The row

`Item` gains these virtuals, and `CheckResult` these fields:

```cpp
// True for an item whose value is a list of folders rather than a line of text.
virtual bool valueIsFolderList() const { return false; }
// Why a folder cannot go on such a list, in a sentence; empty if it can.
virtual QString whyRefused(const QString &) const { return {}; }

struct CheckResult {
    CheckState state = CheckState::CouldNotTell;
    QString detail;
    QString currentValue = {}; // what is applied now, for an item that can read it
    QMap<QString, QString> heldFolders = {}; // folders something else already has, with the name to give
};
```

`src/core/folderlist.{h,cpp}` holds the value's format, so the row and the item read and write one shape.

`src/gui/folderlistfield.{h,cpp}` adds `FolderListField`. It shows one line per folder: the path, a `Switch` labelled "Let people add, change and delete files" (off to start, per the user's preference for switches), and a Remove button. Below them is an "Add a folder…" button using `QFileDialog::getExistingDirectory`, as `src/gui/carrypages.cpp` does, disabled at the cap. A refused folder is not added and `whyRefused`'s sentence shows beneath the list. A folder another share already has is refused too, naming that share.

`ItemRow` builds it when `valueIsFolderList()` is true and fills it from `CheckResult::currentValue`. `ItemRow::takesValue()` replaces `valueField()` in `Wizard::workerArguments` and `Wizard::valuesValid`; `valueField()` stays for the text field. `ItemRow::value()` returns the list as §4.2's JSON. An empty list returns an empty string, unless the row was filled with shares when it opened, and then it returns `[]`. So a new user cannot start Samba with nothing to share, and a user who removes the last folder can apply that.

### 4.6 The item

`src/items/networksharesitem.{h,cpp}`, joining the catalogue after `extraDrives`: id `network-shares`, `Level::Configuration`, `installsPackages()` true. `valuePrompt()` returns the list's label. Its sentence is INV-10's.

**Check.** It uses `readService` and `CheckContext::readFile` only, and never `firewall-cmd`.

- `readService` did not answer: `CouldNotTell`.
- `smb`, `nmb` and `wsdd` are all enabled and active, `groundwork-shares.conf` is readable, and `smb.conf` holds the include line: `Done`.
- Anything else, an unreadable file included: `NotDone`. Applying fixes each of those (CLAUDE.md, Item checks never run as root).
- `currentValue` is parsed from `groundwork-shares.conf` whenever it is readable, so a row reading `NotDone` still shows the list. `heldFolders` comes from `smb.conf`'s other sections.
- `detail` names any other shares in `smb.conf`: "Other shares, left as they are: ROMS, PC Games." Reserved names are not listed. Each name passes through `isolated()`.

**Apply.** `Item` gains a second virtual, and the Worker calls it instead of `applySteps`. Its default returns `applySteps(system, context)`, so no other item changes:

```cpp
virtual QList<Step> applyStepsFor(const SystemIdentity &system, const CheckContext &context,
                                  const QString &value) const;
```

`NetworkSharesItem::applySteps` returns `applyStepsFor` with the value now applied. The steps, in order:

1. `zypper -n install`, naming `samba` if `readService` does not find `smb` and `wsdd` if it does not find `wsdd`; absent when both are found.
2. `writeFileStep` for `groundwork-shares.conf`.
3. Append §4.1's lines to `smb.conf` by `dd oflag=append conv=notrunc`, as `ExtraDrivesItem::applySteps` appends to `fstab`; absent when the line is there. The text starts with a newline, for a file with no final one.
4. `touch /etc/samba/smb.conf`.
5. `systemctl enable smb nmb wsdd`.
6. `systemctl restart smb nmb wsdd`.
7. When `readService` finds `firewalld` active: `firewall-cmd --permanent --add-service=samba`, then `--add-service=wsdd`, then `--reload`, as `SshItem::applySteps` does for `ssh`.

Step 4 and the restart serve `/usr/share/samba/update-samba-security-profile`, `smb.service`'s `ExecStartPre`. It runs at start only, so a reload would skip it. It labels new share paths for SELinux, and its AppArmor branch rebuilds its list only when `smb.conf` is newer than that list, so a change to the included file alone would leave a new folder denied. It reads paths through `testparm -s`, which follows the include.

No step runs `testparm` as a gate. A file holding an unknown parameter still exited 0 on 2026-10-30, so it would prove nothing. §4.3 and §4.4 are the defence, since they decide every line written.

### 4.7 Changing the list later

`Worker::run` gains one case: an item whose re-check is `Done` still runs when its `currentValue` is not empty and a value was given that differs from it. Both are compact JSON from one writer, so a plain comparison serves. An item that leaves `currentValue` empty, as `HostnameItem` does, behaves as now.

The item installs packages, so it depends on bringing the system up to date (`docs/design.md`, The levels). Changing the list later still switches that on when updates are waiting.

An empty list leaves Apply greyed out, so the review page says a value is still needed.

## 5. Invariants

- **INV-1** — `isValidValue` refuses every value breaking a §4.3 rule, and accepts `/mnt/Games/PC Games` with a space, and a path holding `=`. *Test:* `tests/tst_networksharesitem.cpp::aRefusedFolderRefusesTheValue`, a data table with one otherwise-valid path per rule. *Breaks when:* a path such as `/mnt/a\n[x]`, `/mnt/a%u`, `~/.ssh` or `/etc` passes.

- **INV-2** — The written file holds exactly one `[global]` plus one section per entry, whatever the folder's name. *Test:* `tests/tst_networksharesitem.cpp::folderNamesCannotAddSections`, with folder names holding `]`, `[global]` and `=`. *Breaks when:* a folder's name reaches a section heading unreplaced.

- **INV-3** — A share name never equals, ignoring case, a section of `smb.conf`, a reserved name, or an earlier entry's name. *Test:* `tests/tst_networksharesitem.cpp::aNameInUseGetsANumber`, with a folder called `roms` beside a `[ROMS]` section, one called `users`, and two called `Games`. *Breaks when:* the clash test compares with case, or the fixed names are dropped.

- **INV-4** — `smb.conf` is only ever appended to, by one include block, once. *Test:* `tests/tst_networksharesitem.cpp::theOwnersConfigIsOnlyAppendedTo`, asserting no step replaces it, the append step's text, and no append step when the line is present. *Breaks when:* a step uses `writeFileStep` on `smb.conf`, or a second run adds a second block.

- **INV-5** — Every share carries `guest ok = yes`, `force user` naming the Worker's user, and `read only` matching its entry; none sets `wide links`. *Test:* `tests/tst_networksharesitem.cpp::eachShareSaysWhoAndWhat`. *Breaks when:* `write` is inverted, or a look-only share omits `force user`.

- **INV-6** — The item is off unless the user switches it on. *Test:* `tests/tst_selection.cpp::networkSharesNeverStartsOn`, with its check `NotDone` in `defaultSelection`. *Breaks when:* it moves to `Level::Essentials`.

- **INV-7** — The check runs no root command and no `firewall-cmd`, and an unreadable file reads `NotDone`, never `CouldNotTell`. *Test:* `tests/tst_networksharesitem.cpp::theCheckNeedsNoRoot`, with a fake runner that records each command and an unreadable `smb.conf`. *Breaks when:* the check asks `firewall-cmd`, or returns `CouldNotTell` for an unreadable `smb.conf`, which would make the Worker skip it forever.

- **INV-8** — A `Done` item runs when the given value differs from `currentValue`, and is skipped when they match; an item with an empty `currentValue` is skipped as now. *Test:* `tests/tst_worker.cpp::aChangedValueRunsADoneItem`. *Breaks when:* §4.7's case is missing, or fires for `hostname`.

- **INV-9** — The list reaches the item whole. *Test:* `tests/tst_worker.cpp::aListValueReachesItsItemWhole`, through `--set` with a path holding spaces, `=` and a non-ASCII letter. *Breaks when:* `src/main.cpp` splits on a later `=`, or a caller joins the arguments through a shell.

- **INV-10** — The row says, in words, that anyone on the network can get in with no password, including any network this computer joins. *Test:* `tests/tst_networksharesitem.cpp::theRowSaysAnyoneCanGetIn`, on `applySentence()` with translations unloaded. *Breaks when:* the sentence is softened to drop "anyone" or "no password".

- **INV-11** — Steps run in §4.6's order: the touch comes before the restart, the restart is a restart, and the firewall comes after it. Firewall steps appear only when `firewalld` is active. *Test:* `tests/tst_networksharesitem.cpp::theStepsRunInOrder`, with `firewalld` active and inactive. *Breaks when:* the restart becomes `reload`, or the touch is dropped.

- **INV-12** — An empty list gives an empty value on a fresh row and `[]` on a row that opened with shares; a new folder starts look-only. *Test:* `tests/tst_wizard.cpp::aFolderListGivesItsValue`. *Breaks when:* a fresh row can apply nothing, or a user cannot remove their last folder.

- **INV-13** — On a real system, the boundary holds: a guest reads a look-only share and cannot write to it, adds a file to an add-files share and that file belongs to the user, and cannot follow a symlink out of a share. *Test:* §7's manual recipe. *Breaks when:* `force user`, `read only` or `wide links` behaves other than §4.1 says.

## 6. Failure modes

- **A chosen folder is gone at apply time, such as an unplugged USB disk.** The share is still written. Samba refuses that share until the disk returns, and the others work.
- **A package is unavailable or zypper fails.** The step fails and the item reports it. `groundwork-shares.conf` and `smb.conf` are untouched, since those steps come later.
- **The first restart labels a very large folder.** Relabelling runs in `ExecStartPre` under `smb.service`'s `TimeoutSec=5min`. If it times out, the step fails, the run page says so, and Samba is down until it starts. Applying again resumes, since the script records a share only when it finishes.
- **A restart interrupts someone copying a file.** Accepted. It happens only when the list changes or the item is first applied.
- **Something rewrites `smb.conf` and loses the include line.** The check reads `NotDone` and the next apply adds it back.
- **The firewall is off at apply time, or closed later.** The check cannot see it (§2.4), so the row may read done while other computers are blocked. Changing the list and applying opens it again. `SshItem` has the same limit.
- **A laptop joins a public network.** The shares are open there if that network's firewall zone is the default one. INV-10's sentence says so; it is not defended further (§8).
- **A share's name moves.** Names are worked out on each apply, so removing `Games` can turn `Games 2` into `Games`. Anyone with the old name saved must find it again.
- **Windows refuses guests.** Windows 11 24H2 requires SMB signing by default, and a guest connection cannot be signed, so its client can refuse these shares until a setting is changed on it. Source: <https://learn.microsoft.com/en-us/answers/questions/2189515/windows-11-24h2-and-insecure-guest-logins-settings> (community answers) and <https://learn.microsoft.com/nl-nl/windows/client-management/mdm/policy-csp-lanmanworkstation>. Groundwork cannot change another computer. §15 holds what the row says.

## 7. Tests

Each new test must be seen to fail before its code exists.

| Invariant | Test |
|---|---|
| INV-1, INV-2, INV-3, INV-4, INV-5, INV-7, INV-10, INV-11 | `tests/tst_networksharesitem.cpp`, a new target in `tests/CMakeLists.txt`, using `FileReader` and fake runner fixtures as `tests/tst_extradrivesitem.cpp` does |
| INV-6 | `tests/tst_selection.cpp::networkSharesNeverStartsOn` |
| INV-8, INV-9 | `tests/tst_worker.cpp`. Each drives the built program, whose catalogue is the real one, with fake commands on `PATH`; neither touches Samba |
| INV-12 | `tests/tst_wizard.cpp::aFolderListGivesItsValue`. It does not open the file dialog |
| INV-13 | the recipe below |

These unit tests prove the text and steps Groundwork produces. None of them proves Samba, the firewall or SELinux behave as expected. Only the recipe does.

### Manual recipe (INV-13), in a Tumbleweed VM before release

With a second machine or the host on the same network, and `/srv/look` and `/srv/add` holding a file each, with a symlink in `/srv/look` pointing to `/etc/passwd`:

1. Switch the row on, add both folders, and move the switch for `/srv/add` only. Apply.
2. `smbclient -N -L //<vm>`: both shares are listed.
3. `smbclient -N //<vm>/look -c 'get <file> /dev/null'` succeeds; `put` is refused; `get` of the symlink is refused.
4. `smbclient -N //<vm>/add -c 'put <file> new'` succeeds. In the VM, `ls -l /srv/add/new` names the user.
5. Remove `/srv/add` in Groundwork and apply. Step 2 lists only `look`, and `smb.conf` still has its other sections.
6. Repeat step 3's `get` with the firewall on and with `samba` removed from the zone, to see that the firewall is what lets it through.
7. From a Windows machine, if one is at hand, see whether the computer appears under Network and whether it opens. Record the result in the release notes for GRND-0066.

## 8. Alternatives considered (and rejected)

- **A Samba password.** Claude recommended it. The user chose no password, 2026-10-30.
- **A marked block inside `smb.conf`.** Rewriting part of a file the user edits by hand risks their shares on a parsing mistake. An include needs one appended line.
- **Newline-separated or repeated `--set` values.** A folder may hold almost any character, and repeated `--set` needs a new parsing rule in `src/main.cpp`. JSON has its own escaping, and Qt reads it.
- **Writing the list to a temporary file and passing its path.** This is how `--prepare-drive` and `--setup-choices` work, but a value this small fits an argument, and a file adds a second thing to clean up.
- **Changing `applySteps` to take the value.** That edits every item. A second virtual with a default edits one.
- **A `hosts allow` list of private addresses.** It does not stop a public network's own private range, the firewall is already the boundary, and the IPv6 spelling would go in unmeasured.
- **Reading the firewall in the check through `firewall-cmd`.** It can raise a password box during a survey (§2.4).
- **`testparm` as a gate before restarting.** It exited 0 on a bad parameter (§4.6).
- **`reload` instead of `restart`.** It skips the script that labels new folders (§4.6).
- **Leaving wsdd off.** The roadmap item names wsdd as how Windows finds the computer.
- **Removing shares whose folder is gone.** An unplugged drive would lose its share on the next apply.

## 9. Out of scope

- A password or per-person access — the user's choice, 2026-10-30.
- Editing or removing the user's own shares in `smb.conf`, including the owner's `OS_ISOs` and `InsightVM`, which point at the retired `/mnt/Storage` — deferred; not yet queued.
- Stopping Samba and closing the firewall when the last share is removed. The user's own shares may still need it — deferred; not yet queued.
- Carrying the share list in a saved setup (GRND-0078) — deferred; not yet queued.
- A way to tell Windows to accept guests — deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/tst_networksharesitem.cpp::aRefusedFolderRefusesTheValue` |
| INV-2 | `tests/tst_networksharesitem.cpp::folderNamesCannotAddSections` |
| INV-3 | `tests/tst_networksharesitem.cpp::aNameInUseGetsANumber` |
| INV-4 | `tests/tst_networksharesitem.cpp::theOwnersConfigIsOnlyAppendedTo` |
| INV-5 | `tests/tst_networksharesitem.cpp::eachShareSaysWhoAndWhat` |
| INV-6 | `tests/tst_selection.cpp::networkSharesNeverStartsOn` |
| INV-7 | `tests/tst_networksharesitem.cpp::theCheckNeedsNoRoot` |
| INV-8 | `tests/tst_worker.cpp::aChangedValueRunsADoneItem` |
| INV-9 | `tests/tst_worker.cpp::aListValueReachesItsItemWhole` |
| INV-10 | **`Partial:`** `tests/tst_networksharesitem.cpp::theRowSaysAnyoneCanGetIn` reads the English sentence. **Nothing** checks a translation says the same |
| INV-11 | `tests/tst_networksharesitem.cpp::theStepsRunInOrder` |
| INV-12 | `tests/tst_wizard.cpp::aFolderListGivesItsValue` |
| INV-13 | **nothing** automatic — §7's manual recipe, run in a VM before release |
| §4.1 `wide links` keeps a guest inside a share | **`Partial:`** INV-5's test sees that the file does not set it. **Nothing** but the recipe sees Samba obey it |
| §6 firewall closed after apply | **nothing** — a check cannot read the firewall without root (§2.4) |

## 11. Cross-doc impact

- `CHANGELOG.md`, on shipping.
- `README.md`, if it lists the items.
- `docs/design.md`, The levels: the new item if that section names Configuration's items.
- `CLAUDE.md`'s position lines, as the state moves.
- `translations/*.ts`: new text, through `update_translations`.

## 12. Cold-eyes loop log

Rows live in `../reviews/GRND-0066-network-shares-loop-log.md`.

## 13. Resource cost

No state held in memory beyond the row's list, capped at 20 entries. One new test target. On the user's system, the `samba` and `wsdd` packages, installed only when the row is applied. No new build dependency.

## 14. Migration / compatibility

The user's existing `smb.conf` shares stay as they are and are named on the row. `map to guest = bad user` applies to the whole of Samba once included, so a machine set to another value changes behaviour for its other shares too. A folder already shared by `smb.conf` cannot be added (§4.5), so nothing is shared twice by Groundwork.

## 15. Open questions

- **Does Leap 16.0 offer `wsdd`, and `wsdd` as a firewalld service?** Settled 2026-10-30: yes. Leap 16.0's `repo/oss` metadata lists `wsdd` 0.8, whose files include `wsdd.service` and `/usr/lib/firewalld/services/wsdd.xml`. No system needs a separate path.
- **Should the row warn about Windows 11?** Decided here unless the recipe shows otherwise: yes, one sentence, "Windows 11 may refuse a folder with no password until a setting is changed on that computer." The exact Windows behaviour is from the sources in §6, and is not measured here.
