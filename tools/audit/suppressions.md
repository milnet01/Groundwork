# Static-analysis suppressions — Groundwork

The classes of tool finding this project does not act on, and why. Read by
`check-code` step 7. Written by `close-findings` (`~/.claude/skills/close-findings/SKILL.md` § 6).

A class is listed only after a spot-check of its hits. Style classes are not
suppressed here; they are queued as one roadmap item (GRND-0094).

## Drop rules

| tool | rule pattern | justification | action |
|---|---|---|---|
| clang-tidy | `bugprone-throwing-static-initialization` | File-scope `QString` constants, such as `kCarriedPackagesId` in `src/carry/carrieditems.h`. They can throw only when memory runs out at start-up. Spot-checked 2026-10-29. | suppress |
| clazy | `non-pod-global-static` | The same file-scope `QString` constants, reported by clazy. Spot-checked 2026-10-29. | suppress |
| clang-tidy | `bugprone-signed-bitwise` | Shifts of positive `constexpr qint64` constants, such as `kVfatLimit` in `src/carry/carry.h`. Spot-checked 2026-10-29. | suppress |
| cppcheck | `unusedStructMember` | cppcheck misreads struct fields used in other files. `FlatpakApp::app` is reported unused but `src/carry/carry.cpp` reads it. Spot-checked 2026-10-29. | suppress |
| cppcheck | `missingInclude` | The `*.moc` files CMake's AUTOMOC generates into `build/`. Spot-checked 2026-10-29. | suppress |
| typos | path `translations/*.ts` | Translations are not English text. Spot-checked 2026-10-29. | suppress |
| typos | `hda` / `HDA` | Intel HD Audio, the sound hardware the sound-firmware item names (`tests/tst_soundfirmwareitem.cpp`). | suppress |
| typos | path `ROADMAP.md` | Generated from the roadmap store. Its hits are fragments of commit hashes and German words. Spot-checked 2026-10-29. | suppress |
| clang-tidy | `bugprone-unchecked-optional-access`, path `tests/*` | A test reads the value after `QVERIFY` or `QVERIFY2` on it, which returns from the test when it is empty. The analyser does not see that return. Spot-checked 2026-10-29 in `tests/tst_carry.cpp`. | suppress |
