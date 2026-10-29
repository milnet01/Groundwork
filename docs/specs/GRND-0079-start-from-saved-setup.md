<!-- ants-spec-format: 1 -->
# GRND-0079 — Start a new install from a saved setup

**Status:** accepted (2026-10-20). No review gate: this project cancels it (CLAUDE.md, Reviews and specs).
**Kind:** feature.
**Source:** ROADMAP GRND-0079 (user request, 2026-10-15).

**Blocked by:** GRND-0078.

**Layman:** After reinstalling, Groundwork reads the file you saved,
switches on everything you kept, and puts your programs, software
sources and settings back.

## 1. Goal

On a new install, the setup job can start from a file saved by the carry
job (`docs/specs/GRND-0078-saved-setup-file.md`, "the saved file's spec").
The kept Groundwork items start switched on. The file's software sources,
programs, Flatpak apps and settings appear as items, switched on. One run
puts them back, settings last and as the user. Where the file holds
passwords or keys, the last page offers to remove them from the file, or
to delete the file.

## 2. Problem

Nothing reads a saved file outside the tests. `readSetupFile`,
`secretMembers` and `withoutSecrets` (`src/carry/carry.h`) have no caller
in `src/`. Four things make this expensive to get wrong:

1. **Items are fixed today.** `catalogue()` (`src/items/catalogue.cpp`)
   builds every item once, and `Worker::run` refuses an id it lacks with
   `UNKNOWN_ITEM`. The file's entries must become items both the wizard
   and the Worker can name (`docs/design.md`, The Worker's interface).
2. **The file crosses a trust boundary twice.** It comes from a stick
   and may be edited. Its sources reach `zypper` as root, and its
   settings are written into the home folder.
3. **A setup is large.** This machine marks 340 packages as asked for,
   and has 16 Flatpak apps (measured 2026-10-20 with
   `zypper --no-refresh search --installed-only -t package | grep -c '^i+'`
   and `flatpak list --app --columns=application | wc -l`). One
   unknown name makes `zypper -n install` install nothing (exit 104).
4. **Settings replace files.** A restore must never lose a file the new
   install already has.

## 3. Scope decisions (agreed with the user)

Ruled by the user, 2026-10-20:

- Programs are one item, "Programs from your saved file", with a switch
  per program inside it, all on, installed in one go. A program the new
  system cannot find is skipped and named at the end. Flatpak apps and
  settings follow the same shape.
- A kept item that accepts a licence (`Item::acceptsLicence`: the NVIDIA
  driver, the extra fonts) starts switched off, noted "You had this
  before."
- A file a setting replaces is kept as a numbered copy
  (`dolphinrc.~1~`, `~2~`), so a second restore loses nothing.
- A kept setting that holds secrets starts switched on, marked as
  holding secrets.

From `docs/design.md`, Carrying a setup over, ruled by the user
2026-10-15: sources before programs; settings as the user, after the
programs; secrets removed or the file deleted only when the user says so.

Claude's calls, made here and open to the user:

- Each software source is its own item, so one failing source does not
  stop the others.
- A source with `gpgcheck` off, or from a file saved on a different
  system, starts switched off, with a note saying why.
- A carried source's signing key is imported without asking, as
  Packman's is (`packmanSteps`).

## 4. Design

### 4.1 Choosing the file

When the setup job is chosen, `WelcomePage` shows a switch, "Start from
a saved setup", off by default, and a file field with a Browse button.
The dialog filters `groundwork-setup-*.tar` and opens on a mounted USB
stick when there is one.

On Next, the wizard reads the file with `readSetupFile`, as the user. A
refused file keeps the page, showing the reason. A readable file adds a
page, `SavedSetupPage`, after the welcome page and before the level
pages.

### 4.2 Carried items

Carry builds the file's items. It now links `groundwork_items`, which
`docs/design.md`, What may depend on what, allows.

```cpp
// src/carry/carrieditems.h
struct CarriedItems
{
    std::vector<std::unique_ptr<Item>> owned; // in run order
    QStringList keptItems;     // kept ids this catalogue has
    QStringList unknownItems;  // kept ids it lacks, named on the page
};
// The items for the kept parts of manifest, read from file.
CarriedItems carriedItems(const Manifest &kept, const QString &file,
                          const Catalogue &catalogue);
// The catalogue's items, then the carried ones.
Catalogue withCarried(const Catalogue &catalogue, const CarriedItems &carried);
```

The catalogue keeps its non-owning pointers. Whoever holds the
`CarriedItems` keeps them alive: the `Wizard`, or `Worker::run`.

Carried items come after every Groundwork item, in this order:

| Id | One per | Check | Apply |
|---|---|---|---|
| `carried-source:<alias>` | zypper source | Done when `/etc/zypp/repos.d` holds a source with this alias or this `baseurl` (`parseRepoFile`) | § 4.3 |
| `carried-packages` | file | Done when `rpm -q` finds every kept name; the detail names the rest | § 4.4 |
| `carried-flatpaks` | file | Done when `flatpak list --app --columns=application,installation` lists every kept app | one `flatpak install -y --noninteractive --system` or `--user` `REMOTE APP` step per missing app, `alsoOk = {1}` |

A Flatpak remote becomes no item. `flatpak remote-add` of its URL alone
exits 1, "public key not found", and the manifest holds no key (measured
2026-10-20 in a Tumbleweed container with Flathub's beta repository).
The page names each one for the user to add (user, 2026-10-20). An app
from it fails its step and is named by the `HINT`.
| `carried-settings` | file | `tar --diff` of the kept paths: 0 Done, 1 not done unless it reports only owners and modes, 2 couldn't tell | § 4.5 |

The colon in a carried id cannot clash with a Groundwork id: none has one,
and § 4.7 of the saved file's spec admits none in a package name or item
id.

`carried-packages` installs packages, so it depends on `system-update`
(`Catalogue::dependencies`). `carried-flatpaks` depends on `flathub` when
a kept app comes from `flathub`. No carried item depends on a carried
source: a failed source would otherwise skip every program.

An item with nothing kept is not built.

### 4.3 Software sources

Each zypper source is written whole, as the old machine had it,
`$releasever` and all. It is refreshed from a folder of its own first,
and moved into place only when that works, so a source that fails is
never added:

1. `mkdir -p /run/groundwork/repos.d`.
2. `writeFileStep("/run/groundwork/repos.d/<alias>.repo", …)`, holding
   `[alias]`, `name`, `baseurl`, `priority`, `enabled`, `autorefresh`,
   `gpgcheck` and, when set, `gpgkey`. Every value has passed § 4.7 of
   the saved file's spec, and a name holds no control character
   (`readManifest`), so no value can add a line.
3. `zypper -n -D /run/groundwork/repos.d --gpg-auto-import-keys refresh
   <alias>`, `Tool::Zypper`.
4. `mv -f -- /run/groundwork/repos.d/<alias>.repo
   /etc/zypp/repos.d/<alias>.repo`.

Measured 2026-10-20 in a `registry.opensuse.org/opensuse/tumbleweed`
container: the staged refresh of an unreachable source exits 4 and
`/etc/zypp/repos.d` stays without it; a reachable one exits 0, and after
the move `zypper refresh` finds it up to date. A source written disabled
is skipped by `zypper refresh` with exit 0, so refreshing it proves
nothing. An enabled source that fails does not stop installs: `zypper
install` exits 106, which the Worker counts as success.

A source Groundwork's own item already added, such as Packman, reads
Done by its `baseurl`, so it is not added twice.

### 4.4 Programs

`SavedSetupPage` shows `carried-packages` as one row with a switch per
program inside it, all on. The switched-off names are the item's
choices (§ 4.6).

The apply is one step, as root:

```
zypper -n --ignore-unknown install NAME…
```

Measured 2026-10-20 in a `registry.opensuse.org/opensuse/tumbleweed`
container: with `--ignore-unknown`, an unknown name and `hello` installs
`hello` and exits 0; without it, an unknown name and `nano` installs
nothing and exits 104.

A name that clashes with the system still makes zypper install
nothing: `-n` answers the conflict with cancel, exit 4 (GRND-0086,
seen in the VM on 2026-10-26 with `kernel-default` beside
`kernel-default-base`). Exit 8, rpm refusing the transaction, is
treated the same. The Worker then installs the names in halves,
splitting each half that fails the same way, until a name fails
alone. Exit 4 and 8 also mean the system failed, such as a download
that could not finish (`man zypper`, EXIT CODES). So a name that
failed alone is taken to clash only when another part of the same
step installed; when no part installs, the step fails with zypper's
exit code. Once more than eight names have failed alone, the step fails
with zypper's exit code, and the rest are not tried. A file saved before GRND-0078
§ 4.2 left out the installer's choices may still hold them; they are
dropped as the file is read (GRND-0078 § 4.7).

After a carried item ends `ok`, the Worker runs its check again. When
the check is not Done, a `HINT` with the item's id carries the check's
detail: "Not installed: …". So the run page names what was
skipped.

### 4.5 Settings

The `carried-settings` row lists each kept setting by its name, with a
switch, all on. A setting with `"secret": true` is marked "Holds saved
passwords or keys".

The apply is one step, as the user (`needsRoot = false`), the kept
paths on standard input, NUL-separated:

```
tar --extract --file FILE -C HOME --backup=numbered --no-overwrite-dir
    --verbatim-files-from --null --files-from=-
```

`HOME` is `QDir::homePath()`, as in `Worker::saveSetup`. Its check reads
"couldn't tell: settings are copied as the user, never as root" when the
Worker runs as root (`geteuid() == 0`).

Measured 2026-10-20 with GNU tar 1.35, in a scratch tree:

- A replaced file is kept as `dolphinrc.~1~`, then `~2~` on a second
  restore. `--backup=simple` overwrote the first copy on the second
  restore.
- An archive holding the link `.config/evil -> /outside`, then
  `.config/evil/pwned`: tar exits 2 and writes nothing outside.
- A home whose `.config/kde` is a link to a folder outside the home
  folder: tar refuses to write through it ("Invalid cross-device link")
  and exits 2.
- `tar --diff`: 0 after an extract, 1 when a file is missing or its time
  or size differs, 2 when a path is not in the archive.
- `tar --diff` also exits 1 on "Uid differs", "Gid differs" and "Mode
  differs" (measured 2026-10-29, an archive written with
  `--owner=12345 --mode=…`). A restore as the user leaves the user's
  own owner and umask, so a diff reporting only those is Done. A
  missing file is named on stderr alone, so any stderr is not done.

A link member is extracted as a link. It is made as the user, so it
points only where the user can already write.

After the run, a `HINT` says to log out and back in, so running apps
read the restored settings.

### 4.6 From the wizard to the Worker

The wizard writes `$XDG_STATE_HOME/groundwork/restore-choices.json`:

```json
{ "file": "/run/media/ants/STICK/groundwork-setup-pc-2026-10-15.tar",
  "manifest": { "format": 1, "packages": ["act"], "…": "…" } }
```

`manifest` lists only what stayed switched on, in the format of § 4.6 of
the saved file's spec. The run starts as:

```
groundwork --worker --setup-choices CHOICES_FILE [--lang LANG] [--set ID=VALUE]... ID...
```

The Worker reads the file again with `readSetupFile`. Every entry of the
choices' manifest must be in the file's manifest. Otherwise it says
`SETUP_REFUSED|<reason>` and exits 2, before any step. It then builds
the carried items from the choices' manifest, and runs the ids as
`Worker::run` does today.

### 4.7 The page

`SavedSetupPage` shows, in order:

- the file's name, its `created` date and, when `system.id` differs from
  this system's `ID`, "Saved on a different system: <id>";
- the kept Groundwork items this version lacks, as "Not offered by this
  version: …";
- the entries the reader skipped (`readSetupFile`'s `skipped`);
- the Flatpak remotes, for the user to add (§ 4.2);
- a row per carried item, with its switch and inner switches.

On leaving the page, each kept Groundwork item whose check says not done
is switched on with `Catalogue::switchOn`, except one that accepts a
licence, which gets "You had this before." A kept item that reads not
needed here is shown as such.

### 4.8 Secrets at the end

When the file holds any setting with `"secret": true`, kept or not, the
run page shows after `RunPage::runFinished`:

> This saved file holds saved passwords or keys. Anyone with the file can
> read them.

with two buttons:

- "Remove them from the file" runs
  `groundwork --worker --remove-secrets FILE`: the saved file's spec,
  § 4.8, as one item, id `remove-secrets`, every step as the user. The
  Worker reads the file again first. Its last step lists the file and
  checks that no member is under a secret setting.
- "Delete the file" asks once more, naming the file, then runs
  `groundwork --worker --delete-setup-file FILE`: `rm -f -- FILE`, as
  the user.

Neither runs unless pressed. Leaving the page keeps the file as it is.

## 5. Invariants

- **INV-1** — A kept Groundwork item whose check says not done starts
  switched on, unless it accepts a licence; one that accepts a licence
  starts off, noted.
  *Test:* `tests/tst_wizard.cpp`, a file keeping `firewall` and a
  licence item; the rule isolated is `acceptsLicence`, since both check
  not done.
  *Breaks when:* the licence item starts on, or a kept item starts off.

- **INV-2** — No entry reaches a command unless the Worker read it from
  the file itself: an entry in the choices that the file lacks refuses
  the run with `SETUP_REFUSED`, before any step.
  *Test:* `tests/tst_worker.cpp`, choices naming package `evil` that the
  file lacks; exit 2, no `zypper` call recorded.
  *Breaks when:* the Worker builds items from the choices file alone.

- **INV-3** — A carried id is never a Groundwork id, and the Worker
  accepts it only with `--setup-choices`.
  *Test:* `tests/tst_worker.cpp`, `carried-packages` without
  `--setup-choices`; `UNKNOWN_ITEM`, exit 2.
  `tests/tst_carry.cpp`, every carried id holds a colon or starts
  `carried-`, and none is in `catalogue()`.
  *Breaks when:* a carried id is built from a name alone, such as
  `act`.

- **INV-4** — One program the system lacks does not stop the others,
  and the run page names it.
  *Test:* `tests/tst_worker.cpp`, a fake `zypper` that installs only
  known names; the step carries `--ignore-unknown`, the item ends `ok`,
  and a `HINT` for `carried-packages` names the unknown one.
  *Breaks when:* the install runs without `--ignore-unknown`, or the
  check is not run again.

- **INV-5** — Settings are written as the user, never through
  `sudo`, and only under the home folder.
  *Test:* `tests/tst_worker.cpp`, a run with `carried-settings`; the
  `tar --extract` call is not recorded by the fake `sudo`.
  `tests/tst_carry.cpp`, real `tar` with an archive holding a link to a
  folder outside, then a file under the link, run through § 4.5's step;
  `tar` exits 2 and nothing is written outside.
  *Breaks when:* `needsRoot` is true, or `-C` is not the home folder.

- **INV-6** — A file a setting replaces is kept: after two restores
  over one existing file, the home folder holds it as `~1~`.
  *Test:* `tests/tst_carry.cpp`, real `tar`, § 4.5's step run twice.
  *Breaks when:* `--backup=simple`, or no backup.

- **INV-7** — A source is written whole and once: its `.repo` file holds
  every field the file kept, `$releasever` unexpanded, and a source with
  the same `baseurl` already present reads Done.
  *Test:* `tests/tst_carry.cpp`, a fixture `repos.d` holding Packman
  under another alias, and a kept Packman source; Done. A kept `home:`
  source with `$releasever`; the step's input holds it.
  *Breaks when:* the source is compared by alias alone.

- **INV-8** — A source with `gpgcheck` off, or from a file whose
  `system.id` differs, starts switched off, with its reason.
  *Test:* `tests/tst_wizard.cpp`, a file from `opensuse-leap` with one
  such source each.
  *Breaks when:* every carried item starts on.

- **INV-9** — Secrets are removed or the file deleted only when the user
  presses the button; afterwards no member is under a secret setting,
  or the file is gone.
  *Test:* `tests/tst_wizard.cpp`, a run finishing with a secret file;
  no Worker started until a button is pressed.
  `tests/tst_worker.cpp`, `--remove-secrets` on a real file; `tar
  --list` shows no member under `.ssh`; `--delete-setup-file`; the file
  is gone.
  *Breaks when:* the page removes them on leaving, or the removal's last
  step is missing.

- **INV-10** — A program that clashes with the system does not stop
  the others, and the run page names it (GRND-0086, 2026-10-29).
  *Test:* `tests/tst_worker.cpp`, a fake `zypper` that exits 4 while a
  clashing name is in the list; the rest are installed, the item ends
  `ok`, and the `HINT` names the clashing one. A second fake fails
  every install; the step fails within a few dozen `zypper` runs. The
  same fake over a few names; the step fails.
  *Breaks when:* the Worker does not split a failed install, splits
  without a limit, or ends a step `ok` when no part installed.

## 6. Failure modes

- **The stick is pulled during the run:** the settings item's `tar`
  fails, and the item fails. Pulled before that item starts, its check
  cannot compare the file, so the item is skipped, saying so. Items
  before it are unaffected.
- **A source cannot be reached:** its item fails, and it is never added
  (§ 4.3). Programs only it carries are named by the `HINT`.
- **The file changed between the page and the run:** INV-2 refuses the
  run if an entry is gone; the run page shows the reason.
- **A Flatpak remote:** named on the page, never added (§ 4.2).
- **A setting cannot be written** (a link out of the home folder, a full
  disk): `tar` exits 2; the files before it are written, with their
  copies; the item fails, naming the path.
- **Removing secrets fails midway:** `tar --delete` edits the file in
  place (INV-4 of the saved file's spec), so a part-done removal may
  leave a file the reader refuses. The run page says so. The settings
  are already on this computer by then.
- **Stop requested:** between items, as today (`docs/design.md`,
  Stopping).

## 7. Tests

- `tests/tst_carry.cpp`: INV-3, INV-5, INV-6, INV-7, with fixture
  `repos.d`, fake `rpm` and `flatpak`, and real `tar`.
- `tests/tst_worker.cpp`: INV-2, INV-3, INV-4, INV-5, INV-9, INV-10, driving the
  built program with its fakes.
- `tests/tst_wizard.cpp`: INV-1, INV-8, INV-9.
- Manual, before release: a real file from this machine on a vfat
  stick, restored in a fresh Tumbleweed VM.
- Each test is seen to fail before its code exists.

## 8. Alternatives considered (and rejected)

- **One item per program** — 340 rows and 340 `zypper` runs; the user
  chose one list (§ 3).
- **`dependsOn` from programs to sources** — the manifest does not say
  which source a package came from, and one failed source would skip
  every program.
- **`zypper addrepo` with flags** — it cannot set `gpgkey`, and expands
  nothing the `.repo` file keeps; writing the file restores the source
  exactly.
- **Extracting to a staging folder, then copying** — twice the disk, and
  tar's own `-C` checks already keep writes in the home folder
  (measured, § 4.5).
- **`--backup=simple` with a suffix** — a second restore overwrites the
  first copy (measured, § 4.5).
- **Values through `--set`** — `kValuePlaceholder` replaces one
  argument, and a list of 340 names is many.

## 9. Out of scope

- Unreadable settings files at save time — tracked by GRND-0085.
- Patterns, zypper services, the rest of `.local/share`, encryption —
  deferred by the saved file's spec, § 9; not yet queued.
- Telling which program came from which source — deferred; not yet
  queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/tst_wizard.cpp` |
| INV-2 | `tests/tst_worker.cpp` |
| INV-3 | `tests/tst_worker.cpp`, `tests/tst_carry.cpp` |
| INV-4 | `tests/tst_worker.cpp` |
| INV-5 | `tests/tst_worker.cpp`, `tests/tst_carry.cpp` |
| INV-6 | `tests/tst_carry.cpp` |
| INV-7 | `tests/tst_carry.cpp` |
| INV-8 | `tests/tst_wizard.cpp` |
| INV-9 | `tests/tst_wizard.cpp`, `tests/tst_worker.cpp` |
| INV-10 | `tests/tst_worker.cpp` |
| A carried source's packages are what the user expects | **nothing** — the source is restored as it was; what it serves now is its owner's |

## 11. Cross-doc impact

- `docs/design.md`, Carrying a setup over: programs, Flatpak apps and
  settings are one item each, with a switch per entry (user,
  2026-10-20).
- `docs/reference/worker-interface.md`: `--setup-choices`,
  `--remove-secrets`, `--delete-setup-file`, `SETUP_REFUSED`.
- `translations/*.ts`: the new text, drafted
  (`docs/reference/translations.md`).
- README: starting from a saved file, once shipped.
- CHANGELOG: an Added entry.

## 12. Cold-eyes loop log

Rows live in `../reviews/GRND-0079-start-from-saved-setup-loop-log.md`.

## 13. Open questions

None. Both were settled by container runs on 2026-10-20: a failed
source is never added (§ 4.3), and a Flatpak remote is named, not added
(§ 4.2).
