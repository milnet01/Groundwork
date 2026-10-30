<!-- ants-spec-format: 1 -->
# GRND-0065 — Copy your GitHub projects into a folder you choose

**Status:** accepted (2026-10-30). No review gate: this project cancels it (CLAUDE.md, Reviews and specs).
**Kind:** feature.
**Source:** ROADMAP GRND-0065 (user request, 2026-10-10; design choices 2026-10-30).

**Layman:** Sign in to GitHub once, choose which of your projects to keep, choose a folder, and Groundwork copies them onto this computer.

## 1. Goal

The first page offers a fourth job, "Copy your GitHub projects to this computer". The job installs `git` and `gh` if they are missing, signs the user in from the window, lists the user's own repositories with a switch each, and copies the switched-on ones into a chosen folder. Private ones work once signed in. Everything but installing the two programs runs as the user. The last page of a set-up run offers the job too.

## 2. Problem

1. No part of Groundwork talks to GitHub. `src/items/catalogue.cpp`'s `dev-claude-tools` row installs `gh` and goes no further.
2. An item cannot hold this. A row is drawn before anything is applied, but the list needs `gh` installed and signed in first. Item checks also have no answer for "done": a list of projects chosen today says nothing about tomorrow's check.
3. The roadmap item asks for no personal data in the source. A list of the owner's projects would be exactly that.
4. Signing in is a security boundary. Groundwork must never hold the token.

## 3. Scope decisions (agreed with the user)

**The user's, 2026-10-30:**

- It is a job of its own, not a row, also offered at the end of a set-up run. Claude recommended this; §2.2 says why. The roadmap headline's "Item" is superseded.
- Switches start on, except forks. There are switch-all buttons.

**Earlier (user, 2026-10-10, in the roadmap item):** sign in once, list the repositories, pick which to copy, choose the folder; copying runs as the user.

**Claude's, following from those. The user may overrule any of them:**

- Only repositories the signed-in account owns are listed, which is what `gh repo list` gives. Organisations and other people's projects wait (§9).
- The account is whichever `gh` has active. The page names it. Choosing between accounts waits (§9).
- A folder that already exists at the destination is never touched. The project is skipped and named.
- Installing the two programs does not run a full system update first (§8).

## 4. Design

### 4.1 Where the code lives

A new part, **GitHub**, in `src/github/`, depending on Core only, as Items do. It names commands, parses their output and returns steps. It runs nothing. The pages are `src/gui/githubpages.{h,cpp}`. `Job` (`src/gui/wizard.h`) gains `Job::GitHubProjects`, and `wizardMode` in `src/main.cpp` adds it to `setup.jobs`. A live system is not offered it, by `Wizard`'s existing rule.

### 4.2 The pages

| Page | Shows | Leaves when |
|---|---|---|
| Start | Whether `git` and `gh` are found (`rpm -q git gh`). When missing: a sentence naming them | Next: to the install run page if missing, else onward |
| Install | `RunPage`, running `groundwork --worker --github-tools [--lang LANG]` | The run ends ok |
| Sign in | Skipped when signed in. A "Sign in to GitHub" button, then the one-time code in large text, a Copy button and an Open browser button | `gh` exits 0 |
| Projects | "Signed in as NAME." One row per repository, with a `Switch`, its name, "Private" when it is, and its size. "Switch all on" and "Switch all off" | At least one switch is on |
| Folder | A folder dialog as `src/gui/carrypages.cpp` uses, the total size and the free space (`readFolderSpace`) | The folder can be used (§4.6) |
| Copy | `RunPage`, running `groundwork --worker --copy-github CHOICES_FILE [--lang LANG]` | The run ends |

The last page of a set-up run gains a "Copy your GitHub projects…" button beside the one `Wizard::backToChoices` serves. It goes to Start.

### 4.3 Signing in

Signed in means `gh auth status` exits 0. It exited 1 with an empty config folder (measured 2026-10-30, `gh` 2.102.0). The name comes from `gh api user --jq .login`.

The Sign in page starts one process as the user, through `QProcess`, with no shell and standard input closed:

```
gh auth login --hostname github.com --git-protocol https --web
```

Measured 2026-10-30 with no terminal, in an empty `GH_CONFIG_DIR`: it prints the one-time code and `https://github.com/login/device` to standard error, then waits. It opened no browser, so Groundwork's button does, through `QDesktopServices::openUrl`. The code is matched wherever it appears on standard error:

```regex pcre2
\b[A-Z0-9]{4}-[A-Z0-9]{4}\b
```

| input | expected |
|---|---|
| `! One-time code (ABCD-1234) copied to clipboard` | `ABCD-1234` |
| `First copy your one-time code: WXYZ-9876` | `WXYZ-9876` |
| `Open this URL to continue in your web browser: https://github.com/login/device` | no match |

The second row's wording is from no measurement: `gh` printed the first on a machine with a clipboard. The pattern does not depend on the words.

Leaving the page or closing the window ends the process with `QProcess::terminate`. It holds no root and has no half-finished state that matters, since `gh` stores nothing until sign-in succeeds. This is the one non-read-only command the Wizard starts itself. It needs a live two-way hold on the page, which the Worker's markers and stop file cannot give (§8). `docs/design.md` is amended to say so.

The token goes from GitHub to `gh`'s own store. Groundwork never asks `gh` for it, and never logs the code.

### 4.4 The list

```
gh repo list --limit 1000 --json nameWithOwner,isPrivate,isFork,diskUsage
```

Through `CheckContext::run`, with a 60-second limit. On 2026-10-30 this read the owner's account and returned valid JSON. `parseRepositories` keeps an entry only when its name matches:

```regex pcre2
^[A-Za-z0-9_.-]+/(?!\.{1,2}$)[A-Za-z0-9_.][A-Za-z0-9_.-]*$
```

| input | expected |
|---|---|
| `someone/my-project.git2` | `someone/my-project.git2` |
| `someone/-oUpload` | no match |
| `someone/..` | no match |
| `someone/a b` | no match |

A row starts on unless `isFork`. A list holding exactly 1000 says "Only the first 1000 are shown." `diskUsage` is GitHub's figure in KiB, and a checkout can differ from it, so the Folder page calls its total "about".

### 4.5 Copying

The wizard writes `$XDG_STATE_HOME/groundwork/github-choices.json`:

```json
{ "folder": "/home/someone/Projects", "repos": ["someone/project"] }
```

`Worker::copyGithub(choicesPath)` trusts nothing, as `Worker::saveSetup` does. It validates the folder by §4.6 and every name by §4.4, and refuses the run before any step if one fails or two share a last part. Then it makes the folder (`mkdir -p --`) and runs `gh auth setup-git`, so a later `git push` in a copied project works. A failure of the second is a `HINT`, not a stop, since the copies do not need it.

Then one item per repository, id `github:<owner>/<name>`, title the name, in the order given. `stopRequested` is read between them. Destination `DEST` is `<folder>/<name>`.

1. `DEST` exists, link or empty folder included: `STEP_END` with `skip`, saying it is already there.
2. All as the user:

```
env GIT_TERMINAL_PROMPT=0 GIT_ASKPASS= SSH_ASKPASS= gh repo clone OWNER/NAME <folder>/.NAME.partial
mv -n -- <folder>/.NAME.partial <folder>/NAME
```

The `env` is why a revoked sign-in fails instead of waiting for a password. KDE sets `SSH_ASKPASS`, and git would otherwise open its box for each project. A leftover `.NAME.partial` from a power cut makes `gh` fail, and the detail names it for the user to delete; it is never deleted for them. One project failing does not stop the rest.

### 4.6 Which folder

`GithubFolderProblem`, called by the page and the Worker, returns a sentence or nothing. The folder must be absolute and equal to its own `QDir::cleanPath`. It or its nearest existing parent must be writable by the user. It must not be `/` or under the system folders GRND-0066 §4.3 lists, and it must not be below a dot-folder in the home folder. A folder that does not exist yet is made when the copy starts (§4.5). The page starts at `~/Projects`.

Less free space than the total shows a warning and does not stop Next, since the total is only an estimate (§4.4).

### 4.7 Installing the programs

`--worker --github-tools` runs one item, id `github-tools`, with one root step: `zypper -n install git gh`, as `Step::Tool::Zypper`. The password goes through `RunPage` as on any run. It joins no catalogue, so it is no row and no saved-setup entry.

## 5. Invariants

- **INV-1** — No Groundwork code reads the token: nothing runs `gh auth token` or opens `hosts.yml`, and no step or log line holds a one-time code. *Test:* `tests/tst_github.cpp::theTokenIsNeverRead`, recording every command against a fake `gh`, and `tests/tst_github.cpp::theCodeIsNotLogged`. *Breaks when:* a step calls `gh auth token`, or the sign-in output reaches `Worker::say` or the log.

- **INV-2** — A forked repository starts off and every other one starts on, private ones included. *Test:* `tests/tst_github.cpp::forksStartOff`. *Breaks when:* `isFork` is ignored, or `isPrivate` is read in its place.

- **INV-3** — A name from `gh` or the choices file reaches a command only if it matches §4.4's pattern; the Worker refuses the whole run otherwise, before any step. *Test:* `tests/tst_github.cpp::aBadNameRefusesTheRun`, one bad name in each position, with a fake `gh` recording that it never ran. *Breaks when:* `someone/-oUpload` or `someone/..` reaches `gh repo clone`.

- **INV-4** — An existing destination is never changed, whatever it holds. *Test:* `tests/tst_worker.cpp::anExistingFolderIsSkipped`, with a folder, an empty folder and a link at `DEST`, each left byte-identical. *Breaks when:* `mv` loses `-n`, or the existence test misses a dangling link.

- **INV-5** — After a failed clone, nothing appears at `DEST`, and the other projects still run. *Test:* `tests/tst_worker.cpp::aFailedCopyLeavesNoProject`, a fake `gh` failing the first of three. *Breaks when:* the clone goes directly to `DEST`, or a failure ends the run.

- **INV-6** — A copy never waits for a password: every clone runs with `GIT_TERMINAL_PROMPT=0` and both askpass variables empty. *Test:* `tests/tst_github.cpp::aCopyNeverAsks`, on the step's arguments. *Breaks when:* the `env` prefix is dropped. Whether git obeys is §7's recipe.

- **INV-7** — Only the install step needs root. *Test:* `tests/tst_github.cpp::onlyTheInstallIsRoot`, on every step `github-tools` and `copy-github` build. *Breaks when:* any copy step sets `needsRoot`.

- **INV-8** — Stopping takes effect between projects: a stop file written during one leaves that project complete and starts no other. *Test:* `tests/tst_worker.cpp::aStopEndsTheCopyBetweenProjects`. *Breaks when:* `stopRequested` is not read in the loop.

- **INV-9** — This job's code and tests hold no real account or repository name. `src/items` names the owner's public OBS repositories on purpose, and is outside this. *Test:* `git grep -n milnet -- src/github src/gui/githubpages.cpp tests/tst_github.cpp` → no output. *Breaks when:* a fixture or default uses the owner's name.

- **INV-10** — On a real system a private repository copies, and `git pull` works in it afterwards. *Test:* §7's manual recipe. *Breaks when:* `gh`'s behaviour differs from §4.3 or §4.5.

## 6. Failure modes

- **No network, or GitHub is down.** `gh auth status` fails too, so the Sign in page shows, and signing in fails with `gh`'s own message on the page. It cannot tell this from being signed out.
- **The code expires, or the user declines in the browser.** `gh` exits non-zero. The page says so, and the button starts a fresh attempt.
- **The sign-in store is unavailable.** `gh` falls back to a plain file in `~/.config/gh`, with a warning shown on the page. The saved-setup job already marks that folder secret (`src/carry/settingsgroups.cpp`).
- **`gh` is too old for a `--json` field.** The list reads as damaged and the page says "couldn't read the list". No project is touched.
- **The disk fills during a copy.** That clone fails, git removes its partial folder, and the next projects also fail, each named.
- **A project is very large.** Stop waits for it to finish (`docs/design.md`, Stopping). No progress bar beyond `git`'s own lines.
- **A project needs Git LFS or submodules.** It copies without them, with git's warning in the log (§9).
- **The wrong account is active.** The page names it. Fixing it is in a terminal, for now (§9).
- **Package names differ on Leap 16.** See §15.

## 7. Tests

Each new test must be seen to fail before its code exists.

| Invariant | Test |
|---|---|
| INV-1, INV-2, INV-3, INV-6, INV-7 | `tests/tst_github.cpp`, a new target in `tests/CMakeLists.txt`, with fake commands on `PATH` |
| INV-4, INV-5, INV-8 | `tests/tst_worker.cpp`, driving the built program with a fake `gh` |
| INV-9 | the command in the clause; `scripts/local-ci.sh --docs` gains it |
| INV-10 | the recipe below |

These tests use a fake `gh`, so they prove what Groundwork asks of it, not what GitHub does.

### Manual recipe (INV-10), in a Tumbleweed VM before release

1. On a fresh VM, choose the job. Enter the password when asked. `rpm -q git gh` then finds both.
2. Sign in with the code in a browser on the VM. The Projects page names the account and marks private ones.
3. Leave one private project and one public project on. Choose a new folder. Copy.
4. `git -C <folder>/<private> pull` succeeds and asks nothing.
5. Revoke the sign-in on GitHub's website, add another project and copy again. It fails, and no password box appears.
6. Copy again with the first project's folder still there. It says already there, and the folder is unchanged.
7. Repeat steps 1 and 2 on Leap 16 (§15).

## 8. Alternatives considered (and rejected)

- **A catalogue row.** Claude's first framing and the roadmap's headline. §2.2 rules it out; the user chose the job.
- **Talking to GitHub directly.** It needs Qt Network and a registered OAuth app, whose id would be personal to one account. `gh` already does both, and its token store is its own problem.
- **Signing in through the Worker.** A sign-in must be cancellable while it waits, and the Worker's only control is a stop file read between items.
- **One item with all the clones.** Stopping and per-project failure both need one item each.
- **`git clone` of a URL.** `gh repo clone` handles the private case's credentials without first running `gh auth setup-git`, so a setup failure cannot lose the copy.
- **Refusing Next when the disk looks too small.** The figure is GitHub's, not the checkout's, so it would block copies that fit.
- **A full system update before the install.** It is right for a run of many installs (`docs/design.md`, The levels). Here it would turn fetching a few repositories into a long download. zypper still resolves what the two packages need.

## 9. Out of scope

- Organisation repositories, and repositories owned by others — deferred; not yet queued.
- Choosing between several signed-in accounts — deferred; not yet queued.
- Updating projects already copied — deferred; not yet queued.
- Git LFS and submodules — deferred; not yet queued.
- Putting the choice of projects in a saved setup — deferred; not yet queued.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/tst_github.cpp::theTokenIsNeverRead`, `tests/tst_github.cpp::theCodeIsNotLogged` |
| INV-2 | `tests/tst_github.cpp::forksStartOff` |
| INV-3 | `tests/tst_github.cpp::aBadNameRefusesTheRun` |
| INV-4 | `tests/tst_worker.cpp::anExistingFolderIsSkipped` |
| INV-5 | `tests/tst_worker.cpp::aFailedCopyLeavesNoProject` |
| INV-6 | **`Partial:`** `tests/tst_github.cpp::aCopyNeverAsks` reads the arguments. **Nothing** automatic sees git obey them; §7 step 5 does |
| INV-7 | `tests/tst_github.cpp::onlyTheInstallIsRoot` |
| INV-8 | `tests/tst_worker.cpp::aStopEndsTheCopyBetweenProjects` |
| INV-9 | The clause's `git grep`, in `scripts/local-ci.sh --docs` |
| INV-10 | **nothing** automatic — §7's manual recipe, run in a VM before release |
| §4.3 the code is found on standard error | `tests/tst_github.cpp`, the table in §4.3. **Nothing** sees a later `gh` keep printing it |

## 11. Cross-doc impact

- `docs/design.md`: the shape's job list, The parts (GitHub), What may depend on what, and the Wizard's one started command. Done with this spec.
- `docs/discovery.md`: this job is outside S1 to S6. A sign for it is added when it ships.
- `docs/reference/worker-interface.md`: `--github-tools` and `--copy-github`.
- `README.md`, `CHANGELOG.md`, on shipping.
- `translations/*.ts`: new text, through `update_translations`.
- `CLAUDE.md`'s position lines, as the state moves.

## 12. Cold-eyes loop log

Rows live in `../reviews/GRND-0065-copy-github-projects-loop-log.md`.

## 13. Resource cost

The list is held in memory, capped at 1000 entries. One new test target and one new source folder. On the user's system, `git` and `gh` when missing, plus the projects chosen. No new build dependency.

## 14. Migration / compatibility

None. No saved file, setting or existing page changes shape. A machine already signed in to `gh` skips the Sign in page.

## 15. Open questions

- **Does Leap 16 ship `gh`?** Unverified. `dev-claude-tools` names it, and that row has not been read on Leap for this. Settle by the §7 recipe's step 7, or Leap's package metadata as GRND-0066 §15 did. If it does not, the job reads "not needed here" on Leap: the Start page says so and stops.
