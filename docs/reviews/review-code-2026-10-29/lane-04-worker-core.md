I read all 20 subject files in full. The main problem is in the new install-in-parts code: when every part fails for a reason that has nothing to do with clashing packages, a saved setup with 8 or fewer programs can end "ok" with nothing installed. I found nothing at Critical or High.

**Line counts as read** (the last numbered line in each file): `worker.cpp` 622, `worker.h` 81, `privilege.h` 37, `privilege.cpp` 52, `item.h` 131, `checkrunner.h` 24, `checkrunner.cpp` 41, `commandrunner.h` 30, `commandrunner.cpp` 35, `markers.h` 26, `markers.cpp` 32, `selection.h` 58, `selection.cpp` 120, `statepaths.h` 16, `statepaths.cpp` 17, `systemidentity.h` 40, `systemidentity.cpp` 85, `translations.h` 39, `translations.cpp` 107, `CMakeLists.txt` 142.

**Already in my context when I arrived, before reading anything:**
- the global `~/.claude/CLAUDE.md`;
- the review-lane agent definition;
- a SessionStart hook note about the Ants MCP verbs;
- a userEmail context block;
- environment details (date, platform).

There was no git snapshot and no memory index. The shared context also quoted part of the project `CLAUDE.md`. I resolved no disagreement between the brief and the agent file.

**Lane:** depth pass. I opened no test files and kept `tests/**` out of every search.

## Critical (0)

## High (0)

## Medium (3)

- [dim 16] `worker.cpp:39` — `bool zypperClashed(int code) { return code == 4 || code == 8; }`, read with `worker.cpp:147` — `return --*lonesLeft >= 0;`
  - **The problem.** zypper(8) EXIT CODES define 4 as any error from the ZYPP library and 8 as any error while committing, a failed download included. Neither is specific to a clash.
  - **What happens.** If the system itself fails, for example the network drops during the commit, every part fails. The code splits the list down to single names and counts each one as a "lone clash" that succeeded. With 8 or fewer names (`kMostLoneClashes` is 8) the budget never runs out. `runStep` then returns true and the item ends `STEP_END|…|ok` with nothing installed.
  - **Large lists.** With many names it takes about log₂(n) + 16 extra zypper runs before it fails, each one possibly a long download timeout.
  - **Uncertain.** I have not confirmed that a network failure during commit gives exit 8 rather than something else.
  - **Fix:** treat a single name as clashing only if an earlier part in the same step succeeded. Or stop splitting after the first part fails with no part succeeding, and return false.

- [dim 2] `worker.cpp:131-134` — `if (zypperSucceeded(code) || (step.namesFrom > 0 && zypperClashed(code) && installInParts(...))) return true;`
  - **The divergence.** `design.md` § What every part does the same way says an apply step's zypper exit codes are read with "OneUp's rule: 0, 100–103 and 106 are success". The code now also counts 4 and 8 as success when the clash is down to single names.
  - **Where else it is missing.** Neither `design.md` nor `worker-interface.md` mentions installing in parts, or that a step can end ok with names left out.
  - **Which side is wrong:** probably the document, which looks out of date with GRND-0086. I was not given the GRND-0086 spec, so this is also listed under Open questions.
  - **Fix:** take the documentation change to `review-contract`.

- [dim 2] `item.h:51-54` — `// …When those names clash with the system, the Worker installs them in parts, so one name does not stop the rest`, with `worker.cpp:147` — `// the item's check, run after, names it`
  - **What the comment promises.** That a name dropped after a clash gets named by the item's check, run afterwards.
  - **What the code does.** That check runs only `if (success && isCarriedId(id))` (`worker.cpp:284`). The only caller that sets `namesFrom` today is `carrieditems.cpp:132`, a carried item, so the promise holds for now.
  - **The risk.** Any non-carried Zypper step that sets `namesFrom` would drop names in silence and still end ok.
  - **Fix:** run the after-check whenever a step ended through `installInParts`'s lone-clash path, not only for carried ids. Or narrow the `Step::namesFrom` comment to carried items only.

## Low / Info

- [dim 2] `worker.cpp:127-129` — `if (code == 106) say(…HINT…)`. Only the first full run is checked for exit 106. A part run inside `installInParts` that exits 106 is counted as success with no HINT, though the design says "the user is told". **Fix:** check for 106 in `installInParts` too.
- [dim 16] `worker.cpp:146-150` — with an empty names list (`namesFrom == argv.size()`), `first(0)` and `mid(0)` are both empty. If zypper returned 4 or 8 for that, the function would recurse without end. If `namesFrom > argv.size()`, `first()` hits a Qt assert. I believe this cannot happen today: package names are checked, and an empty install should exit with a different code. **Fix:** at the top, `if (names.isEmpty()) return false;`.
- [dim 3] `worker.cpp:72-77` / `markers.cpp:21` — command output is echoed line by line on the same channel as the markers. A line from a command that starts with `@@NAME@@` reads as a marker, for example an RPM scriptlet printing `@@DONE@@|…`. The interface says "Every other line is the output of a command the Worker ran", which cannot be guaranteed. The only realistic source already runs as root. **Fix:** escape a leading `@@` in echoed lines.
- [dim 2] `markers.h:24`, `systemidentity.h:28,32` — the parsers `parseMarker`, `parseOsRelease` and `readSystemIdentity` lack the `[[nodiscard]]` that `cpp.md` § Idioms asks for on parser return types. [tool: clang-tidy]
- INFO: `CommandRunner` (`commandrunner.cpp`) has no general guard against a check raising a polkit password box (known trap 2). It cannot have one, because the fix is per command (for example `--no-ask-password`), which belongs to the Items lane. I did not check the items.
- INFO: known trap 1 (a root-only check). The Worker's re-check runs as the user, as the contract says. I found nothing in this lane that makes it worse or fixes it.
- Dimension 4: nothing found. 5: nothing found. 9 and 11: not weighted. In passing, the log and the manifest files get default permissions rather than owner-only, but I did not look into it. 12: N/A, no standard named. 13: no unconditional locale issue seen (checks run with `LC_ALL=C`). 15 and 17: not weighted, nothing seen.
- Dimension 8: nothing found. The keep-alive timer only fires inside `runCommand`'s nested event loop, and a blocking `QProcess::execute` there does not re-enter. The `m_catalogue` swap in `runFromSetup` is single-threaded and is restored.
- Dimension 10: the log promise ("output is mirrored") is kept by `say()`. Ignoring a failure to open the log is documented (`worker.cpp:174`).
- Dimension 2b: nothing found. `namesFrom` (`carrieditems.cpp:132`), `alsoOfferedBy` (`wizard.cpp:678`, `claudecodeitem.h:20`) and `entries(…, true)` (`carry.cpp:291`) each have a non-test caller.

## Covered by spec and looks correct

- **Root, in `privilege.{h,cpp}`:** `sudo -A -p %p -v` with `SUDO_ASKPASS` and the askpass variable set only for that child; the terminal fallback; `asRoot` as `sudo -n --`; the keep-alive started by the Worker process itself.
- **Option injection:** checked and refuted. Carried package names must start with a letter or digit (`carry.cpp:89`, `^[A-Za-z0-9]…`), so none can start with `-` and inject a zypper option. `installInParts` keeps the argv prefix intact, including the root wrapper.
- **The run loop, in `worker.cpp`:** the re-check before apply, how outcomes are mapped, the AUTH-failed path, the 103 re-run, the `needs-rebooting` 102 hint, the carried after-check HINT, and every exit code against the interface table.
- **`CMakeLists.txt`:** the new `groundwork_carry` (links Items and Core) and what Worker and gui link both match § What may depend on what. Core links Qt Core only. The new `claude-code.asc` resource is wired.
- **Unchanged core files:** `markers.cpp` escapes `|` and line breaks; `systemidentity.cpp` reads ID and VERSION_ID only and never combines the two os-release files; `selection.cpp` matches its header contracts.

## Open questions

- Does the GRND-0086 spec define "ok with names left out" for a Zypper step, and did the design document's exit-code rule miss being updated? I cannot tell which side is wrong.
- `worker.cpp:232-244`: the comment says a dependency "neither done nor in the list" blocks, but the code blocks only on `NotDone`. A dependency left out of the list that reads "couldn't tell" therefore does not block. If the same dependency is in the selection, it does block, through `SkippedOther`. The design does not cover dependencies that were not selected.
- `worker.cpp:240,243` look up `find(dep)->title()` without a null check. For the built-in catalogue `orderProblems` guards this. Whether `withCarried` keeps that guarantee for carried items is outside my files.

## 3 items to fix first

1. **The lone-clash success in `installInParts`.** A system failure (exit 4 or 8) on a saved setup with 8 or fewer programs reports "ok" with nothing installed. It is the only path here where the run states something false.
2. **The after-check gate at `worker.cpp:284`.** It makes the `Step::namesFrom` promise ("the check names it") true for every Zypper step, not just carried ones, before a second caller arrives.
3. **The design document's zypper exit-code rule.** Take it to `review-contract`, so the documented success rule matches what the Worker now does.
