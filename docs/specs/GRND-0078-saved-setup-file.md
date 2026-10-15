<!-- ants-spec-format: 1 -->
# GRND-0078 — Gather a setup and save it to a file

**Status:** accepted (2026-10-15). No review gate: this project cancels it (CLAUDE.md, Reviews and specs).
**Kind:** feature.
**Source:** ROADMAP GRND-0078 (user request, 2026-10-15).

**Blocked by:** GRND-0077.  **Blocker for:** GRND-0079.

**Layman:** Groundwork can list what is set up on this machine, let you
keep or drop each part, and save it as one file for your next install;
saved passwords and keys are left out unless you switch them on.

## 1. Goal

From the wizard's "carry this setup over" job, a user sees four groups
of rows: Groundwork's done items, programs they asked for, software
sources they added, and settings app by app. Each row has a switch. The
user picks a folder, and the Worker writes one file there. The file's
format, defined here, is what GRND-0079 reads on the new install
(discovery S6; `docs/design.md`, Carrying a setup over).

## 2. Problem

Nothing in Groundwork gathers a setup or writes a file a later install
reads. The design places this in a new part, Carry, which names
commands and returns steps and runs nothing itself (`docs/design.md`,
What may depend on what). Three things make it expensive to get wrong:

1. The file is a contract between two jobs and two installs. A file
   saved by this release must still be read by later ones.
2. The file is read on a new install, where its contents reach `zypper`,
   `flatpak` and `tar`. It may have been edited, or damaged on a stick.
3. Settings can hold secrets. A file that carries them is a risk once it
   leaves the machine; the user must be able to see that, and later to
   remove them from the file alone.

## 3. Scope decisions (agreed with the user)

- The four kinds of setup, and that the user keeps or drops each part
  (user, 2026-10-15; discovery S6).
- Settings that hold secrets start switched off; switching one on warns
  (user, 2026-10-15).
- One file, saved where the user chooses; settings and choices only,
  never documents or photos (user, 2026-10-15; discovery, Not a backup
  tool).

Claude's calls, made in this spec and open to the user:

- The file is an uncompressed tar archive (§ 8 says why).
- `~/.local/share` is data, not settings, and is not offered, except
  for the few settings folders § 4.4 names.
- A settings row bigger than 100 MiB starts switched off, its size shown.

## 4. Design

### 4.1 Groundwork's items

Carry takes the check results the wizard already has. An item is
gathered when its check is "already done" and it is not a preparation
(`Item::isPreparation`). Its id is recorded; values (such as the
computer's name) are not, so an item that takes one asks again.

### 4.2 Programs

Packages, through `CheckContext::run`, without root:

```
zypper --no-refresh search --installed-only -t package
```

A row whose first column is `i+` is gathered; its name is the second
column, trimmed (measured 2026-10-15: `i+ | act | …`). Patterns are
not gathered.

Flatpak apps:

```
flatpak list --app --columns=application,origin,installation
```

Tab-separated (measured 2026-10-15). Each line gives app id, remote and
`system` or `user`.

### 4.3 Software sources

zypper sources are read from `/etc/zypp/repos.d/*.repo` through
`CheckContext::entries` and `readFile`, which keeps `baseurl` as written,
`$releasever` and all, rather than expanded as `zypper lr` shows it.
Each file is world-readable (measured 2026-10-15). Kept per source:
alias, name, baseurl, priority, enabled, autorefresh, gpgcheck, gpgkey.

A source is not gathered when its `baseurl`, with any scheme, starts
with one of the distribution's own trees, which a fresh install adds:

```
download.opensuse.org/tumbleweed/
download.opensuse.org/update/tumbleweed/
download.opensuse.org/slowroll/
download.opensuse.org/update/slowroll/
download.opensuse.org/distribution/leap/
download.opensuse.org/update/leap/
download.opensuse.org/debug/
download.opensuse.org/source/
codecs.opensuse.org/
```

A source a Groundwork item adds, such as Packman, is gathered like any
other; the item finds it by URL on the new install, as OneUp's does
(`OneUpItem::applySteps`, `src/items/packman.h::repositoryAlias`). A `.repo` file that cannot
be read or parsed is a row saying "couldn't tell", switched off and
unswitchable, naming the file.

Flatpak remotes other than `flathub`:

```
flatpak remotes --columns=name,url,options
```

`options` holds `system` or `user`.

### 4.4 Settings

Offered, each a candidate entry, paths relative to the home folder:

- every entry directly in `.config`;
- every dot-entry directly in the home folder, except `.cache`,
  `.config`, `.local` and `.var`;
- `.var/app/<id>/config` for each Flatpak app that has one (not its
  `data` or `cache`);
- from `.local/share`, only `applications`, `color-schemes`, `konsole`,
  `kxmlgui5`, `plasma` and `fonts`.

A table in Carry (`carry/settingsgroups.cpp`) groups entries into named
apps — "KDE Plasma desktop" takes `kdeglobals`, `kwinrc`, `plasma*` and
the other Plasma files — and an entry no group claims is a row of its
own, named by the entry. A row's size is `du -s -b -- <paths>`.

The same table marks secrets. At least: `.ssh`, `.gnupg`, `.pki`,
`.password-store`, `.netrc`, `.git-credentials`, `.docker`, `.aws`,
`.kube`, `.mozilla`, `.thunderbird`; in `.config`: `google-chrome*`,
`chromium`, `BraveSoftware`, `microsoft-edge*`, `vivaldi`, `opera`,
`gh`, `gcloud`, `rclone`; and the `.var/app` folders of
`org.mozilla.firefox`, `org.mozilla.Thunderbird`, `com.google.Chrome`,
`com.brave.Browser`, `org.chromium.Chromium`, `com.microsoft.Edge` and
`com.discordapp.Discord`.

A row starts switched on unless it is secret or bigger than 100 MiB.
Switching a secret row on shows, beside it: "The saved file will hold
this app's saved passwords or keys. Anyone with the file can read them."

### 4.5 Saving

The save page shows the file name, the folder (a folder dialog; a
mounted USB stick is offered first), the total size of the switched-on
settings, and the folder's free space
(`df --output=avail -B1 -- DIR`) and filesystem
(`findmnt -no FSTYPE --target DIR`). Save is unavailable when the total
exceeds the free space, or exceeds 4 GiB − 1 byte on `vfat`, which
cannot hold a larger file; the page says which, and which rows are
largest.

The file is named `groundwork-setup-<hostname>-<YYYY-MM-DD>.tar`.

The wizard writes `$XDG_STATE_HOME/groundwork/setup-choices.json`,
holding `{"output": "<path>", "manifest": { … }}`, and starts:

```
groundwork --worker --save-setup CHOICES_FILE [--lang LANG]
```

The Worker runs it as one item, id `save-setup`, with the existing
markers (`docs/reference/worker-interface.md`, Markers). Every step is
`needsRoot = false`:

1. Validate the manifest by § 4.7 and every settings path by INV-3, and
   that each exists. Refuse before any step otherwise.
2. Write the manifest to the state folder as `groundwork-setup.json`.
3. Write the archive to `<output>.partial`, the settings paths on
   standard input, NUL-separated:
   `tar --create --format=posix --file <output>.partial -C <state>
   groundwork-setup.json -C <home> --verbatim-files-from --null
   --files-from=-`. Exit 1 ("a file changed as it was read", a running
   browser say) is also ok (`Step::alsoOk`).
4. `tar --list --file <output>.partial` and compare with the manifest
   (INV-5).
5. `mv -f -- <output>.partial <output>`.

Prototyped 2026-10-15 with GNU tar 1.35 in a scratch tree: the archive
lists the manifest first, `tar --delete` removes one setting's members,
and `tar -xOf FILE groundwork-setup.json` prints the manifest.

### 4.6 The file

An uncompressed POSIX tar archive. Members:

- `groundwork-setup.json`, exactly once, written first. Readers find it
  by name, not position.
- The settings, by their paths relative to the home folder. Every such
  path starts with `.`, so none can clash with the manifest.

The manifest:

```json
{
  "format": 1,
  "created": "2026-10-15T10:00:00Z",
  "groundwork": "0.5.0",
  "system": { "id": "opensuse-tumbleweed", "version_id": "20261014" },
  "items": ["media-codecs", "flathub"],
  "packages": ["act", "android-tools"],
  "flatpaks": [
    { "app": "com.spotify.Client", "remote": "flathub", "installation": "system" }
  ],
  "sources": [
    { "kind": "zypper", "alias": "home_milnet_kde-fixes",
      "name": "home_milnet_kde-fixes",
      "baseurl": "https://download.opensuse.org/repositories/home:/milnet:/kde-fixes/openSUSE_Tumbleweed/",
      "priority": 80, "enabled": true, "autorefresh": true,
      "gpgcheck": true, "gpgkey": null },
    { "kind": "flatpak", "name": "kdeapps",
      "url": "https://distribute.kde.org/kdeapps.flatpakrepo",
      "installation": "user" }
  ],
  "settings": [
    { "name": "Dolphin", "paths": [".config/dolphinrc"],
      "secret": false, "bytes": 1234 }
  ]
}
```

Only what the user kept is listed; a dropped part is absent. `system`
is `ID` and `VERSION_ID` from `/etc/os-release`, so GRND-0079 can tell a
Tumbleweed file from a Leap one.

### 4.7 Reading the file

Carry reads a file with `tar --list --file FILE` and
`tar -xOf FILE groundwork-setup.json`, both through `CheckContext::run`.
It is built here, so the round trip is tested before GRND-0079 relies on
it. It refuses the whole file, with a plain reason, when:

- the manifest is missing, appears twice, or is not JSON;
- `format` is above 1: "This file was saved by a newer Groundwork";
- any member breaks INV-3.

It skips one entry, saying why, when a field fails its pattern below.
Unknown keys are ignored, so a later format 1 writer may add fields.

```regex pcre2
^[A-Za-z0-9][A-Za-z0-9+._-]*$
```

| input | expected |
|---|---|
| `android-tools` | `android-tools` |
| `libstdc++6` | `libstdc++6` |
| `-oRoot` | no match |
| `a b` | no match |

That is a package name, and an item id. A Flatpak app id, a remote's
name and a source's alias follow their own patterns:

```regex pcre2
^[A-Za-z_][A-Za-z0-9_-]*(?:\.[A-Za-z_][A-Za-z0-9_-]*){2,}$
```

| input | expected |
|---|---|
| `com.spotify.Client` | `com.spotify.Client` |
| `org.kde.kate` | `org.kde.kate` |
| `spotify` | no match |
| `-com.a.b` | no match |

```regex pcre2
^[A-Za-z0-9][A-Za-z0-9:._+-]*$
```

| input | expected |
|---|---|
| `home_milnet_kde-fixes` | `home_milnet_kde-fixes` |
| `download.opensuse.org-oss` | `download.opensuse.org-oss` |
| `--gpg-auto-import-keys` | no match |

A URL must start `https://`, `http://` or `ftp://` and hold no space or
control character.

### 4.8 Removing secrets

GRND-0079 removes secrets from a file by: `tar --delete --file FILE`
with every member under a `secret` setting's paths, then
`tar --delete` of `groundwork-setup.json` and `tar --append` of a
manifest without those settings. This works only on an uncompressed
archive (INV-4).

## 5. Invariants

- **INV-1** — A file written by `save-setup` reads back to an equal
  manifest; a manifest with an unknown key reads; `format` 2 is refused.
  *Test:* `tests/tst_carry.cpp`, round trip through real `tar` in a
  temporary folder, plus two hand-made manifests.
  *Breaks when:* the reader requires a key a later writer may drop, or
  accepts a format it does not know.

- **INV-2** — A secret row starts switched off, and is recorded with
  `"secret": true` when kept.
  *Test:* `tests/tst_carry.cpp`, a fixture home with `.ssh` and
  `.config/BraveSoftware`; `tests/tst_wizard.cpp`, the warning shown
  when one is switched on.
  *Breaks when:* a browser profile starts on, or the flag is lost when
  written.

- **INV-3** — Every settings path, in the choices file and in a file
  read, is relative, has no `..` component, starts with `.`, and is not
  `.cache`, `.local` or `.var` alone; every archive member but the
  manifest lies under a path the manifest lists (trust boundary: the
  choices file, and a saved file from a stick).
  *Test:* `tests/tst_carry.cpp`, archives made in the test holding
  `../x`, `/etc/x`, and a member no setting lists; each refused.
  `tests/tst_worker.cpp`, a choices file with `../x`; no `tar` call
  recorded.
  *Breaks when:* a member is extracted that the manifest does not list.

- **INV-4** — Secrets can be removed from a saved file alone: after
  § 4.8's steps the file lists no member under a secret setting, and
  every other member and the manifest's other entries are unchanged.
  *Test:* `tests/tst_carry.cpp`, § 4.8's steps run with real `tar` on a
  round-tripped file.
  *Breaks when:* the file is compressed, so `tar --delete` fails.

- **INV-5** — The file at `output` is complete or absent: the run ends
  `ok` only when the list of the archive matches the manifest, and only
  then is `<output>.partial` renamed.
  *Test:* `tests/tst_worker.cpp`, a fake `tar` that exits 2 on
  `--create`; no file at `output`, the run ends `fail`.
  *Breaks when:* `tar` writes straight to `output`.

- **INV-6** — A source under one of § 4.3's distribution trees is not
  gathered; any other is, with `baseurl` exactly as its `.repo` file
  writes it.
  *Test:* `tests/tst_carry.cpp`, a fixture `repos.d` with the OSS
  repository, a `home:` repository, and one whose `baseurl` holds
  `$releasever`.
  *Breaks when:* the URL is taken from `zypper lr`, which expands it.

- **INV-7** — Only `i+` packages are gathered, by name, trimmed.
  *Test:* `tests/tst_carry.cpp`, a fake `zypper` printing `i`, `i+` and
  `v` rows; the rule isolated is the first column, since every row is
  `-t package`.
  *Breaks when:* `i` rows are gathered, or the name keeps its padding.

- **INV-8** — Only items whose check is "already done" and that are
  not preparations are gathered.
  *Test:* `tests/tst_carry.cpp`, check results with `system-update` and
  one other item done.
  *Breaks when:* `system-update` is recorded.

- **INV-9** — Save is unavailable when the switched-on size exceeds the
  folder's free space, or 4 GiB − 1 byte on `vfat`.
  *Test:* `tests/tst_carry.cpp`, the rule with 5 GiB on `vfat` (no),
  5 GiB on `ext4` with room (yes), and 1 GiB with 0.5 GiB free (no).
  *Breaks when:* the filesystem check is missed, and the stick fills
  with a file that cannot be finished.

- **INV-10** — No field read from a file reaches a command unless it
  matches its pattern in § 4.7; each is one argument, never a shell.
  *Test:* `mcp__ants__spec_conformance` on this spec's patterns;
  `tests/tst_carry.cpp`, a manifest with package `-oRoot` and alias
  `--gpg-auto-import-keys`; both skipped with a reason.
  *Breaks when:* a name starting with `-` is passed to `zypper` as an
  option.

## 6. Failure modes

- **A command is missing** (no `flatpak`): that group is a row saying
  "couldn't tell", and the rest is gathered.
- **`zypper` is locked** by another program: its search fails; the
  programs row says "couldn't tell" and names the lock.
- **The stick is pulled mid-write:** `tar` fails; no file at `output`
  (INV-5); a `.partial` may be left, and the run page says so.
- **A settings file changes while read:** `tar` exits 1; the run ends
  `ok`, and a `HINT` says which app was running.
- **A secret no table knows** (a new browser): it starts switched on.
  The save page always says the file holds the settings as they are,
  and to switch off any app that keeps passwords.
- **Stop requested:** stopping is between items, and this is one item,
  so a started write finishes (`docs/design.md`, Stopping).

## 7. Tests

- `tests/tst_carry.cpp` (new): INV-1, INV-2, INV-3, INV-4, INV-6,
  INV-7, INV-8, INV-9, INV-10, with fixture homes, `repos.d` and fake
  `zypper` and `flatpak`; real `tar` for the round trip.
- `tests/tst_worker.cpp`: INV-3, INV-5, driving the built program with a
  fake `tar`, as it does today with its other fakes.
- `tests/tst_wizard.cpp`: INV-2's warning.
- Manual, before release: save a real setup to a vfat stick, and list
  it with `tar -tvf`. Each test is seen to fail before its code exists.

## 8. Alternatives considered (and rejected)

- **A compressed archive** (`.tar.gz`) — smaller, but `tar --delete`
  cannot edit it, so removing secrets would mean rewriting the whole
  file through a second copy, which may not fit on the stick.
- **A folder instead of one file** — the design and the user chose one
  file; a folder is easy to copy half of.
- **JSON with the settings inside, base64** — a third larger, and
  every settings file held in memory at once.
- **Sources from `zypper lr`** — it expands `$releasever`, so a Leap
  source would carry one version's number to the next.
- **Packages from `/var/lib/zypp/AutoInstalled`** — also readable, but
  `i+` is the user-facing word for the same fact and needs no set
  difference with `rpm -qa`.

## 9. Out of scope

- Reading the file on the new install, turning it into a selection, and
  removing secrets — tracked by GRND-0079.
- The three-job first page — tracked by GRND-0077.
- Patterns, zypper services, and the rest of `.local/share` — deferred;
  not yet queued.
- Encrypting the file — deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/tst_carry.cpp` |
| INV-2 | `tests/tst_carry.cpp`, `tests/tst_wizard.cpp` |
| INV-3 | `tests/tst_carry.cpp`, `tests/tst_worker.cpp` |
| INV-4 | `tests/tst_carry.cpp` |
| INV-5 | `tests/tst_worker.cpp` |
| INV-6 | `tests/tst_carry.cpp` |
| INV-7 | `tests/tst_carry.cpp` |
| INV-8 | `tests/tst_carry.cpp` |
| INV-9 | `tests/tst_carry.cpp` |
| INV-10 | `tests/tst_carry.cpp`; `spec_conformance` for the patterns |
| § 4.4's secrets table covers the apps users have | **nothing** — a new app that keeps passwords is unknown until someone adds it |

## 11. Cross-doc impact

- `docs/reference/worker-interface.md`: `--save-setup`.
- README: the carry job, once shipped.
- CHANGELOG: an Added entry.

## 12. Cold-eyes loop log

Rows live in `../reviews/GRND-0078-saved-setup-file-loop-log.md`.
