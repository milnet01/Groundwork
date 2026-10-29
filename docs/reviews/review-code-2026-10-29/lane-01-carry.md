Subject files and the line counts I read with `Read`: `src/carry/carry.h` 166, `src/carry/carry.cpp` 664, `src/carry/carrieditems.h` 56, `src/carry/carrieditems.cpp` 336, `src/carry/settingsgroups.cpp` 137, `src/core/filereader.h` 36, `src/core/filereader.cpp` 45.

**What I already held on arrival:** the global `~/.claude/CLAUDE.md`, the user-email context block, a SessionStart hook note about the Ants MCP verbs, and my agent definition. There was no project `CLAUDE.md`, no memory index and no git snapshot. Project facts came only from the shared-context file, which the brief told me to read.

**Reading outside the lane source, and why.** The brief and my agent file did not disagree. I read `src/worker/worker.cpp:440-500`, because the fact behind H1 lives there: the save step passes the full manifest to `membersProblem`. I also read `formatMarker`'s body, for L3. Both are checks of a claim, which § The brief is your evidence boundary allows. I opened nothing under `tests/`, and every search excluded it.

## Critical (0)

## High (1)

- **[dim 2]** `carry.cpp:552` and `carry.cpp:541-545`
  - Quoted: `QStringLiteral("--ignore-failed-read")` … `return QCoreApplication::translate("gw::Carry", "The file is missing %1.").arg(p);`
  - **The defect:** a settings path that is itself an unreadable file is left out by tar. One example is a root-owned `~/.viminfo` with mode 0600, a single-path row that starts switched on. When it is left out, `membersProblem` finds no member under that path, and the save fails with "The file is missing .viminfo". `worker.cpp:458-492` emits the "left out" HINT and then calls `membersProblem(members, *manifest)` with the full manifest, so the run ends `fail`.
  - **Why it breaks the contract:** GRND-0078 § 4.5 step 3 (GRND-0085) promises that such a file is left out and a HINT names it, not that the save fails. Even if the comparison tolerated it, the archive's manifest still lists the path, so `readSetupFile` would refuse the file on the new install. The code is the wrong side.
  - **Not tested:** I believe GNU tar writes no header for a regular file it cannot open, but I did not run it.
  - **Fix:** before writing the manifest, remove the paths whose top entry is unreadable (`unreadableSettings` already finds them) and drop any rows left empty.

## Medium (5)

- **[dim 16]** `carry.cpp:316-321`
  - Quoted: `const CommandResult du = context.run(argv, kLongTimeoutMs);` … `sizes.insert(...)`
  - **The defect:** `started`, `timedOut` and the exit code are never looked at. If du is missing, killed or times out, every row gets `bytes = 0`. Large rows then start switched on (`startsOn`), and `saveProblem` checks a total that is too small, so INV-9 decides on bad numbers.
  - **Fix:** when du did not finish, mark the sizes unknown, start those rows off and make Save unavailable.

- **[dim 9]** `carry.cpp:338` and `carry.cpp:343`
  - Quoted: `if (fstype == QLatin1String("vfat") && bytes > kVfatLimit)` / `if (bytes > freeBytes)`
  - **The defect:** `bytes` is the sum of `du -b` sizes, but the archive is bigger. Each member adds a 512-byte header plus padding to 512 bytes, and `--format=posix` probably adds per-file pax headers. The manifest and tar's blocking add more. A settings tree of many small files can pass the check, then fill the stick or go past FAT's 4 GiB limit: exactly INV-9's "Breaks when".
  - **Which side is wrong:** both. The spec says "the total", so the document needs fixing as well.
  - **Fix:** estimate the archive size per file (size rounded up to 512, plus 512, plus about 1 KiB), or add a margin.

- **[dim 2]** `carry.cpp:270-272` against `carry.cpp:473-474`
  - Quoted: `if (!isDistributionSource(source.baseurl) && isSourceUrl(source.baseurl))` against `(s.gpgkey.isEmpty() || isSourceUrl(s.gpgkey)) && s.priority >= 1 && s.priority <= 200`
  - **The defect:** when gathering, only `baseurl` is checked. Three kinds of source get saved and then skipped whole on the new install, with the misleading text "not a name Groundwork can use":
    - a `gpgkey` that is `file:///…`;
    - several space-separated `gpgkey` URLs;
    - a `priority` outside 1–200, including a value `toInt()` cannot parse, which becomes 0 at `carry.cpp:202`.
  - Remotes are already filtered at gather time "with a URL the reader would take" (`carry.h:86`); sources are not.
  - **Fix:** apply the reader's checks when gathering, and show a source that fails them as one that cannot be carried.

- **[dim 3]** `settingsgroups.cpp:104` against `settingsgroups.cpp:30-58`
  - Quoted: `{QStringLiteral("Discord"), {QStringLiteral(".config/discord"), ...`
  - **The defect:** the group table knows that `.config/discord` is the native Discord app, which keeps its login token there. `kSecrets` lists only the Flatpak path, so a native-only Discord row has `secret = false` and starts switched on.
  - The same gap covers the Flatpak config folders of Vivaldi and Opera (`com.vivaldi.Vivaldi`, `com.opera.Opera`), while their native folders are listed.
  - **Which side is wrong:** the spec's list ("At least") matches the code, so this is a gap in the document too. The code is free to add entries.
  - **Fix:** add `.config/discord` and those Flatpak config paths to `kSecrets`.

- **[dim 2]** `carrieditems.cpp:218-226`
  - Quoted: `QStringList argv{QStringLiteral("tar"), QStringLiteral("--diff"), ...}` and `r.exitCode == 1` → NotDone
  - **The defect:** GNU `tar --diff` also reports "Uid differs", "Gid differs" and "Mode differs". After a successful restore, the item can therefore read not-done forever in two cases:
    - the new account's uid differs from the old one;
    - a mode in the archive differs from what extraction left after the umask.
  - The settings would then be re-applied on every run, piling up `~N~` backup copies.
  - **Uncertainty:** this comes from my knowledge of tar, not from a run. The spec's "0 after an extract" was measured on one machine.
  - **Fix:** confirm in a container with a different uid. If it holds, compare contents only, or treat a diff that reports only owner or mode as Done.

## Low / Info

- **[dim 9]** `carrieditems.cpp:83-84` — `{{QStringLiteral("mv"), QStringLiteral("-f"), ... kReposDir + QLatin1Char('/') + m_source.alias + QStringLiteral(".repo")}`. The step overwrites any existing `<alias>.repo` whose contents hold a different alias and URL (the check matched neither), as root, which destroys that source. Fix: refuse, or pick a free file name, when the target exists.
- **[dim 2]** `carry.cpp:460-475`: two manifest sources with the same alias are both kept. That builds two items with the same id `carried-source:<alias>`, and the catalogue's lookup becomes ambiguous. Fix: skip a repeated alias, saying why.
- **[dim 3]** `carry.cpp:74`, `carry.cpp:498` and `carry.cpp:535`: values that failed validation are echoed raw (`skippedName(app.app)`, `.arg(path)`, `.arg(member)`), without the `isolated()` that `carrieditems.cpp` uses. `formatMarker` replaces only `\n`, `\r` and `|`, so escape and bidi-control characters from an edited file reach the page and the `SETUP_REFUSED` marker. Fix: strip control characters and wrap in `isolated()`.
- **[dim 3]** `carry.cpp:619-627`: `tar --extract --to-stdout … groundwork-setup.json` reads a member of any size into memory, and so does `--list` on an archive with millions of members. A crafted file can exhaust memory before validation. Fix: check the member's size first (`tar -tv`), or cap the output.
- **[dim 16]** `carry.cpp:364-365`: if `findmnt` fails, `fstype` stays empty and the FAT check is silently skipped. Fix: treat an unknown filesystem as unknown, the way an unknown free space already is (`carry.cpp:335`).
- **[dim 2]** `carry.cpp:421-425`: a `format` above INT_MAX makes `toInt()` return 0, so the file reads "not a Groundwork setup file" instead of the "newer Groundwork" refusal § 4.7 promises.
- **[dim 7]** `carrieditems.cpp:62-63`: a `.repo` file that cannot be parsed is passed over silently, so a source it already holds reads not-done and is added a second time. Lower than the du case above.
- **INFO, dim 2b:** I searched `src/` for 21 entry points (`readSetupFile`, `secretMembers`, `withoutSecrets`, `unreadableSettings`, `notInFile`, `carriedItems` and the rest). Each has a caller outside `carry/` in `wizard.cpp`, `worker.cpp`, `main.cpp` or `carrypages.cpp`. No zombie features found.
- **INFO, dim 13:** check commands run with `LC_ALL=C` (`commandrunner.cpp:16`), so parsing rpm, zypper and du output is safe across languages. Nothing found.
- **INFO, dim 17:** unknown keys are ignored and format 2 or above is refused. A later format-1 writer that adds a new kind of archive member would be refused whole by this reader (`membersProblem`), because the spec allows new keys but not new members. Nothing else found.
- **INFO, atomic write:** the lane code writes no file itself. In the worker (outside the lane) the manifest goes through `QSaveFile`, and `.partial` is moved within one folder, which is an atomic rename. `tar --delete` edits the file in place; GRND-0079 § 6 already accepts that.
- Dimensions 4, 5, 8, 10, 11, 12 and 15: N/A or not weighted. I found nothing on the paths I read.

## Covered by spec and looks correct

I opened each of these and compared it with the spec:

- the patterns of § 4.2 and § 4.7, which match the spec's regexes;
- `isInstallerChosen`, the list of distribution trees, `kSharedSettings`, and the secrets list as the spec gives it;
- the rules for which settings entries are offered (§ 4.4);
- `createArguments`, `listArguments` and `extractArguments`, which match the spec's command lines;
- the zypper, Flatpak and settings apply steps of GRND-0079 §§ 4.2–4.5;
- `sourceStartsOffWhy`, which matches INV-8;
- `repoFileText`: every value has passed a pattern or a control-character check, so no value can add a line;
- the `.`/`..`/absolute path refusals in `isSettingsPath`;
- `FileReader::entries` and its `hidden` flag.

## Open questions

- Does the list of distribution trees miss trees a fresh install now adds, such as `cdn.opensuse.org` or `download.opensuse.org/ports/`? The code matches the spec, so if it does, the spec is the side to fix.
- `isSourceUrl` accepts `http://` and `ftp://` for `gpgkey`, and the key is then imported automatically, as root, with `--gpg-auto-import-keys`. A key fetched over plain HTTP can be swapped in transit. This was a design decision ("Claude's call"), but it may deserve the user's ruling.
- Both the uid/mode claim (M5) and the claim that tar omits an unreadable file (H1) rest on what I know of GNU tar, not on a run.

## 3 items to fix first

1. **H1 (an unreadable top-level setting fails the save).** A common home folder, one with a root-owned dot-file, makes the whole carry job fail, which contradicts GRND-0085's promise.
2. **M1 (du result ignored).** A silent failure turns the size and space checks into wrong numbers. The result can be a stick that fills, or a large or unexpected row switched on by default.
3. **M3 (Discord's native login token not marked secret).** It is a secret the code's own table already knows about, carried with its switch on. The fix is a one-line addition to the secrets list.
