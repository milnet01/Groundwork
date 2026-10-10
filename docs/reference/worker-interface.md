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

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Every item ended ok or skipped |
| 1 | At least one item failed |
| 2 | Bad usage, or an unknown item id |
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
