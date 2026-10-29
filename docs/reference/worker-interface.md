# The Worker's interface

> **Purpose — so the Wizard, a terminal user and the tests all drive the
> Worker the same way.**

`docs/design.md` (The Worker's interface) names this file as the one
owner of the Worker's command line, markers and stop file. It records
the Worker as built in `src/worker/worker.cpp` and `src/main.cpp`.

## Command line

```
groundwork --worker [--lang LANG] [--set ITEM=VALUE]... [ITEM...]
```

- **`ITEM...`** — item ids from the catalogue, for example
  `system-update`, `media-codecs`, `flathub`. They run in catalogue
  order, whatever order they are given in.
- **No items** — the Worker runs the items the Wizard would start
  switched on.
- **`--lang LANG`** — the language to speak.
- **`--set ITEM=VALUE`** — the value for an item that takes one, such as
  `computer-name=lounge-pc`. The value replaces `{value}` in that item's
  commands. Without a valid value the item is skipped, saying why.

A second form prepares a drive (GRND-0076):

```
groundwork --worker [--lang LANG] --prepare-drive LAYOUT_FILE
```

- **`LAYOUT_FILE`** — the layout the Wizard wrote. Its format is in
  `docs/specs/GRND-0076-prepare-a-drive.md` § 4.4.
- It runs as one item, id `prepare-drive`, with the same markers.
- Item ids and `--set` are ignored in this form.
- It trusts nothing in the file. It ends `fail` before any root step
  when the drive read now is not the one named, or the layout is not
  valid for that drive and this computer.
- On success it prints `HINT` lines saying what to set in the installer
  for each new partition (spec § 4.6).

A third form saves a setup to one file (GRND-0078):

```
groundwork --worker [--lang LANG] --save-setup CHOICES_FILE
```

- **`CHOICES_FILE`** — what the Wizard wrote: `{"output": PATH,
  "manifest": {…}}`. The manifest's format is in
  `docs/specs/GRND-0078-saved-setup-file.md` § 4.6.
- It runs as one item, id `save-setup`, with the same markers, and asks
  for no password.
- It trusts nothing in the file. It ends `fail` before writing anything
  when the manifest breaks the spec's rules, a settings path leaves the
  home folder or is gone, or the output's folder is missing.
- It writes `PATH.partial`, checks its list against the manifest, then
  renames it to `PATH`. On failure nothing is left at `PATH`.
- On success it prints a `HINT` naming the file.

A fourth form runs items with a saved setup's (GRND-0079):

```
groundwork --worker [--lang LANG] --setup-choices CHOICES_FILE [--set ITEM=VALUE]... ITEM...
```

- **`CHOICES_FILE`** — what the Wizard wrote: `{"file": PATH,
  "manifest": {…}}`, the manifest holding only what stayed switched on.
  Its format is in `docs/specs/GRND-0079-start-from-saved-setup.md`
  § 4.6.
- The Worker reads the saved file at `PATH` again. An entry in the
  manifest that the saved file lacks refuses the run with
  `SETUP_REFUSED`, before any step, and exit 2.
- The manifest's parts become items with ids `carried-source:ALIAS`,
  `carried-packages`, `carried-flatpaks` and `carried-settings`. They are
  accepted only in this form.
- After a carried item ends `ok`, its check runs again. A `HINT` with
  its id names what it could not put back, such as a program this
  system lacks.
- `carried-packages` installs in parts when zypper exits 4 or 8. It
  ends `ok` with the names that failed alone left out, and the `HINT`
  names them. It ends `fail` when no part installed, or more than eight
  names failed alone (spec § 4.4).

Two more forms act on a saved file, each as one item asking for no
password (GRND-0079 spec § 4.8):

```
groundwork --worker [--lang LANG] --remove-secrets FILE
groundwork --worker [--lang LANG] --delete-setup-file FILE
```

- `--remove-secrets`, id `remove-secrets`, deletes every member under a
  setting marked secret, and replaces the manifest with one without
  them. It ends `ok` only when no such member is left.
- `--delete-setup-file`, id `delete-setup-file`, deletes the file.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Every item ended ok or skipped |
| 1 | At least one item failed |
| 2 | Bad usage, an unknown item id, or a saved setup refused |
| 3 | Unsupported system |
| 4 | The password was not accepted |
| 5 | Stopped between items |

## Markers

One per line on standard output: `@@NAME@@|field|field`. A field never
holds `|` or a line break (`src/core/markers.cpp`). Every other line is
the output of a command the Worker ran.

| Marker | Fields | Sent |
|---|---|---|
| `UNSUPPORTED` | reason | The system is not supported; the Worker exits 3 |
| `UNKNOWN_ITEM` | id | An id is not in the catalogue; the Worker exits 2 |
| `SETUP_REFUSED` | reason | The choices name what the saved file lacks, or either cannot be read; the Worker exits 2 |
| `STEP_BEGIN` | id, position, total, title | An item starts |
| `AUTH` | `ok` or `failed` | Once, before the first step that needs root |
| `ACTION` | id, label | A step of the item starts |
| `HINT` | id, text | Something the user should know, such as a skipped software source. The id is empty for a note about the whole run, such as a suggested restart |
| `STEP_END` | id, `ok`, `skip` or `fail`, detail | An item ends |
| `DONE` | items ok, items failed, `1` if stopped else `0` | The run ends |

## Stopping

Write the file `$XDG_STATE_HOME/groundwork/stop.request` (or
`~/.local/state/groundwork/stop.request`). The Worker checks for it
between items and stops there. It deletes a stop file left by an
earlier run when it starts.

## Logs

Each run writes everything it prints to
`$XDG_STATE_HOME/groundwork/logs/run-YYYYMMDD-HHMMSS.log`.

## For tests only

`GROUNDWORK_ROOT` points the Worker, and check mode, at a fixture tree
instead of `/`. `tests/tst_worker.cpp` drives the built program this
way, with fake `sudo`, `zypper` and `flatpak` first on `PATH`.
