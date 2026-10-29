# review-code --since v0.4.0 — Groundwork, 2026-10-29

Base: `--since v0.4.0` = 0fad77f, 2026-10-16, 28 commits to HEAD 191c93b. Tree clean at dispatch. The `review-code-last` tag was not moved: no tag exists, and this run was scoped by an explicit ref, not the tag.

Dispatch: single-tier, one parallel batch, every lane through `~/.claude/tools/neutral-lane` (via `tools/lanes`). No lane fell back to the `Agent` tool. Every lane reported `denials=0`.

## Lanes

| Lane | File | Model | Pass | Turns | Usage (neutral-lane stderr) | CRIT | HIGH | MED | LOW |
|---|---|---|---|---|---|---|---|---|---|
| 01 Carry | lane-01-carry.md | claude-opus-5-5 | single-tier | 20 | cache_read=761129 cache_write=123599 output=29654 cost_usd=1.73 | 0 | 1 | 5 | 7 |
| 02 Wizard GUI | lane-02-wizard-gui.md | claude-opus-5-5 | single-tier | 29 | cache_read=1141055 cache_write=135560 output=30929 cost_usd=1.93 | 0 | 0 | 4 | 10 |
| 03 Drives | lane-03-drives.md | claude-opus-5-5 | single-tier | 11 | cache_read=305786 cache_write=80615 output=12010 cost_usd=0.95 | 0 | 0 | 2 | 4 |
| 04 Worker and core | lane-04-worker-core.md | claude-opus-5-5 | single-tier | 30 | cache_read=769931 cache_write=102457 output=22115 cost_usd=1.42 | 0 | 0 | 3 | 4 |
| 05 Items | lane-05-items.md | claude-opus-5-5 | single-tier | 83 | cache_read=1402570 cache_write=146325 output=27658 cost_usd=2.00 | 0 | 0 | 2 | 4 |

`subagent_tokens` and `tool_uses` are not reported by `neutral-lane`; turns and its usage line stand in. Lane cost total: about 8.03 USD (the lanes' own report).

## Partition (whole tree), and what ran

1. Carry — `src/carry/*`, `src/core/filereader.*`. Changed. Ran.
2. Wizard GUI — `src/main.cpp`, `src/gui/` wizard, carrypages, savedsetuppage, runpage (changed); itemrow, switch, themes, askpassdialog (pulled in by `src/core/item.h`). Ran.
3. Drives — `src/drives/*` (changed), `src/gui/preparepages.*` (pulled in). Ran.
4. Worker and core — `src/worker/*`, `src/core/*` bar filereader, `CMakeLists.txt`. Changed. Ran.
5. Items — `src/items/*`. claudecodeitem, claude-code.asc, catalogue changed; the rest pulled in by `src/core/item.h`. Ran.
6. Scripts, packaging and CI — `scripts/`, `packaging/`, `.github/`. Skipped — unchanged since v0.4.0. Not a clean bill.

Excluded by scope: `tests/` (routed to `review-tests`), `docs/`, `translations/` (data, not code).

## Claims a lane flagged as unexecuted, settled by the orchestrator (read-only)

- Lane 05, the key's real fingerprint: `gpg --homedir <scratch> --show-keys --with-fingerprint src/items/claude-code.asc` printed `rsa4096 2026-03-30 [SCE]`, fingerprint `31DD DE24 DDFA B679 F42D 7BD2 BAA9 29FF 1A7E CACE`, no expiry. Matches `claudecodeitem.cpp`. Settled.
- Lane 04, zypper exit codes 4 and 8: `man zypper` reads "4 - ZYPPER_EXIT_ERR_ZYPP A problem is reported by ZYPP library." and "8 - ZYPPER_EXIT_ERR_COMMIT An error occurred during installation or removal of packages." Neither is clash-specific, as the lane said. Whether a dropped network during commit exits 8 is still unsettled; settling it needs a real zypper run with the network cut.
- Lane 03, lsblk START against sysfs: on this machine `lsblk -J -o NAME,START,LOG-SEC` and `/sys/class/block/*/start` give the same numbers (sda1 2048, sda2 2099200, sda3 422834176). Every drive here has 512-byte logical sectors, so this does not tell the units apart. Partly settled.
- Unsettled, and what would settle them: lane 01 H1 (GNU tar writes no member for an unreadable file) and M5 (`tar --diff` reports uid and mode differences) need a tar run on a test tree, ideally in a container with a different uid. Lane 05's `snapper list-configs` polkit question needs a run on a desktop session; not run, because it might raise a password box (user report 2026-10-10).
