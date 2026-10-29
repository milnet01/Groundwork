I'm writing up the findings now. All four lane files have been read in full, along with the spec, the design.md section, and the Worker's call site for layout validation.

**Line counts as read:** `src/drives/drives.h` 156 · `src/drives/drives.cpp` 509 · `src/gui/preparepages.h` 138 · `src/gui/preparepages.cpp` 507.

**In context on arrival:** the global `~/.claude/CLAUDE.md` (machine rules, nothing about Groundwork) and a SessionStart hook message about the Ants MCP. I had no project memory or git snapshot. The shared-context file then gave me quoted project `CLAUDE.md` facts.

**Disagreements resolved:** none between this brief and my agent file.

## Critical (0)

## High (0)

## Medium (2)

- [dim 3] `drives.cpp:328` — `needed += *p.size;`, fed by `drives.cpp:502-503` — `std::optional<qint64>(number(size))`. The layout file is the trust boundary named in INV-4 and INV-8, and `readLayoutFile` only checks that each size is `> 0`. `validate` then adds the sizes up in a signed `qint64` with no upper bound. Two partitions of `2^62` bytes each (whole MiB, above their minimums) add up past the `qint64` limit. That is undefined behaviour, and in practice the total wraps negative. Then `needed > usableSpace(...)` at `:344` is false, so the layout passes. The Worker uses this same `validate` as its only check before `sudo sfdisk` (`worker.cpp`: `if (const QString bad = validate(layout, *before); !bad.isEmpty())`). So a hand-written layout file reaches a root `sfdisk` run, which INV-4 forbids ("Breaks when: a hand-written or corrupted layout file reaches `sfdisk`"). There is a second route. A double such as `1e300` passes `size.toDouble() <= 0`, and `toVariant().toLongLong()` (`:32`) then converts a value outside the `qint64` range, which is also undefined. sfdisk will probably refuse the impossible sizes, but that is sfdisk's check, not the one INV-4 promises. — fix: in `readLayoutFile`, refuse any size that is not a whole number or is larger than the drive's `size`. In `validate`, refuse any `*p.size > usableSpace(...)` before adding it to `needed`. That keeps the sum bounded. [tool: clang-analyzer / bugprone-*, suspicion only]

- [dim 2] **The spec is wrong here, not the code.** `GRND-0076 § 4.1`: *"`START` is in logical sectors."* `drives.cpp:151-153` now reads `START` as 512-byte units (`* 512`), which matches the sysfs ABI your brief cites. On a drive with 512-byte sectors the two readings give the same number, so the spec's measurement on such a drive could not tell them apart. On a drive with 4096-byte sectors, the spec's reading puts each partition eight times further along than it really is. Anyone building the Worker or a test fixture from § 4.1 would then misplace the free space for "beside Windows" (the INV-5 risk). I did not open util-linux's source to confirm that lsblk passes sysfs's `start` through unchanged; the code comment and your brief both say it does. — fix: correct § 4.1 to "512-byte sectors, whatever `LOG-SEC` says" (this one goes to `review-contract`).

## Low / Info

- [dim 2] `preparepages.cpp:426` — `m_hibernateNote->setVisible(!custom && swap > 0 && !hibernateStartsOn(swap, space));`. Spec § 4.3 asks for the note "when off for that reason". The code shows it whatever the switch says, including after the user turns it on. It also states the size in GiB (`gib(swap)`), where the spec says "how many GB". Code is the side that differs; the GB/GiB part may be the spec's slip. — fix: add `&& !m_hibernate->isChecked()`, and pick one unit in the spec.
- [dim 7] `drives.cpp:171-172` — `if (!r.ok()) return {};`. A failed `lsblk` run, or unreadable JSON (`:128`), looks exactly like "no drives". `preparepages.cpp:152` then says "No drive was found. Plug one in", which sends the user to replug a drive when the real problem is a missing or broken tool. The header documents the empty result, but no page reports the failure. — fix: return the failure (an optional or an error string) and show it on DrivePage.
- [dim 2] `drives.cpp:132` — `number(device.value(QStringLiteral("size"))) <= 0`. Drives of size 0 (an empty card reader) are left out. § 4.1 defines a drive as "a `TYPE` `disk` that is not read-only" and does not mention this. The header (`drives.h:45`) does. The spec is the side behind; the behaviour itself is harmless.
- [dim 2] `drives.cpp:82` — an active swap on a dm device appears in `/proc/swaps` as `/dev/dm-N`, but lsblk names it `/dev/mapper/...`, so the `swaps.contains` check at `:85` misses it. INV-1 still holds, because lsblk reports `[SWAP]` as a mount point. The user then reads "is mounted at [SWAP]". Wording only; it does not break the rule.
- [dim 9] No issues found. `ConfirmPage::validatePage` writes through `QSaveFile` (temp file plus rename) and refuses on a failed `commit()`.
- [dim 16] No issues found beyond the dim-7 item. Look again re-surveys, and the Worker re-reads the drive and runs `validate` again before writing.
- [dim 2b] No zombies found. `DrivePage`, `WindowsPage`, `LayoutPage` and `ConfirmPage` are built in `wizard.cpp:523-529`. `surveyDrives` is wired in `main.cpp:116`. `formatFor` and `installerAdvice` are called in `worker.cpp:372/387/399`.
- Dims 4, 5, 8, 10–15 and 17 are weighted out: no issues found in passing. Dims 10 and 12 are N/A, since no commitment for them is named.
- INFO: I did **not** read `standards/languages/cpp.md` or `qt.md`, so nothing here is judged against them, and those standards remain unchecked for this lane.
- INFO: whether sfdisk would refuse the overflowed sizes in the first Medium finding, and whether lsblk's `START` comes straight from sysfs, both need real tools; I could not run anything.

## Covered by spec and looks correct

I opened and checked these against § 4.1–4.5 and INV-1, 5, 7, 8 and 9:
- The `* 512` start (sysfs ABI), and sorting partitions by start.
- The beside script's `start=` and `size=` divided by the logical sector size, which is sfdisk's unit. Both values are 1 MiB-aligned, so they divide evenly on 4096-byte drives.
- `usableEnd` (the 16 KiB backup GPT plus one header sector).
- The suggestion minimums against the actual usable space (23.5/63.5 GiB needed against 24/64 GiB minus about 2 MiB).
- `hibernateStartsOn` (`* 20 <= space`).
- The custom-layout rules in `validate`, which match § 4.3.
- INV-2: `isComplete` follows the switch, and the switch is reset in `initializePage`.
- INV-8: the sfdisk script is built only from fixed names and computed numbers.
- The walk at every depth for INV-1.

## Open questions

- Is sfdisk's own refusal meant to be acceptable as the last line of defence for sizes that pass `validate` but cannot fit (the first Medium finding)? INV-4 reads as no.
- `ConfirmPage::validatePage` shows `layoutPath` without `isolated()`, while every other path is wrapped. Is the path deliberately exempt?

## 3 items to fix first

1. **The size overflow in `validate`/`readLayoutFile`.** It is the only gap I found in the trust boundary, and it sits directly before a root `sfdisk` run.
2. **The spec's § 4.1 `START` unit.** Anyone building to the spec as written on a drive with 4096-byte sectors would bring back the bug v0.4.0 just fixed.
3. **Separating a failed lsblk from "no drives".** Today the user gets wrong advice and has no way to find the real cause.
