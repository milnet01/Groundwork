# Groundwork — instructions for Claude Code

## Where this project is

**State:** 5 — On an item.
**In flight:** 0.5.0: GRND-0076's and GRND-0078's specs accepted; GRND-0077 (the three-job first page) is next. Translations wait for checks (GRND-0033 a fluent Afrikaans reader; zh_CN, zh_TW, ja, ko, hi, zu, xh a Gemini check).

> Keep the two lines above true, and keep them to two lines. They are
> the only position this project records. Everything else about where
> work stands is read off things that cannot lie — whether a spec exists,
> whether tests fail, what `git status` says, whether the roadmap bullet
> is 🚧. Reasoning: [`docs/history/claude-md.md`](docs/history/claude-md.md).
>
> **There is deliberately no `Next:` line.** The next action follows from
> the state — `~/.claude/workflow.md` names what each state leads to —
> and once the roadmap has items, from the roadmap.

## How work is done here

- **`~/.claude/workflow.md`** — the states, the gates, and what "done"
  means. Read in place. This project does not have its own copy.
- **`~/.claude/standards/`** — how to write code, tests, commits,
  documents, releases. Also read in place.

Neither is summarised here. Nor are `~/.claude/CLAUDE.md`'s working
principles, token rules and writing rules, which bind here as they stand.

## Ants MCP

`~/.claude/CLAUDE.md` rule 18 governs the verbs. Findings about Ants MCP
itself go in
`/mnt/Games/Scripts/Linux/Ants_MCP_Feedback_Files/Groundwork_Ants_MCP_Feedback.md`,
through `feedback_log op:"append_finding"`.

## This project's own facts

Everything below is specific to this project, which is why it lives here
rather than in a standard.

### Stack

C++20 and Qt 6, built with CMake and Ninja, tested with Qt Test under
CTest (`docs/design.md`, ADR-0001).

### Build and test

The CI gate is `scripts/local-ci.sh`. GitHub's `ci.yml` calls it and adds
no steps of its own, so a change to CI goes in the script. It has two modes:

- `./scripts/local-ci.sh` — the full gate: builds into `build/` and runs
  `ctest`. `GROUNDWORK_JOBS` sets parallel compiles (default 4).
- `./scripts/local-ci.sh --docs` — every check that needs no build.

Every push runs the gate. `.githooks/pre-push` hands off to
`~/.claude/githooks/pre-push`, which picks `--docs` when every changed
path matches the script's `--docs-glob`. A fresh clone needs this once:

```bash
git config core.hooksPath .githooks
git config ants.gate.command  ./scripts/local-ci.sh
git config ants.gate.docsMode --docs
git config ants.gate.docsGlob "$(./scripts/local-ci.sh --docs-glob)"
```

Linter versions are pinned in `scripts/ci-tools.env`. The workflow
installs those versions, and the gate warns locally when yours differ.

### Translations

Before changing any text a user sees, or a `translations/*.ts` file,
read `docs/reference/translations.md`: refreshing the files, the
`lupdate` context trap, right-to-left marks, and when a language leaves
draft.

### Item checks never run as root

The wizard and the Worker both run as the user; only a step with
`needsRoot` goes through `sudo` (docs/design.md, Root). So the
Worker's re-check before an apply cannot read a root-only file either.
A check that needs root reads "couldn't tell" everywhere, and the
Worker skips the item forever. Read only what the user can read, and
treat an unreadable setting as "not done" where applying fixes it
(GRND-0057's fix, 070964c).

### Roadmap IDs

`GRND-NNNN`, per `roadmap-format.md` § 3.5.1. Commit subjects
are `<ID>: <description>`, per `commits.md`.

The roadmap lives in the Ants roadmap store (project slug `groundwork`).
`ROADMAP.md` is generated from it, so hand edits are discarded by the next
write. Read it with `roadmap_query` and change it with `roadmap_log`.

The roadmap is split by version: one
`## X.Y.Z — <theme>` section per release, in order, then
`## Backlog — no version yet` last. Create a version section with
`roadmap_log op:create_section` when design places items in it.

The prefix is pinned in `.ants/project.json` (`id_format.prefix`), set with
`project_settings op:"set"`. Without it, the first id would be derived from
the directory name as `GROU-0001`.

### Reviews and specs

The user ruled on 2026-10-07: this project runs no reviews. The
`review-contract` gate (`~/.claude/CLAUDE.md` rule 14) is cancelled for
every document here, and no review skill runs as a gate on code or
documents. Write a spec only where `spec-format.md` § 1 says one is
needed; otherwise write none.

### Overrides

Any place this project deliberately departs from a global standard goes
in `docs/standards/`, with the reason. If that directory is empty, there
are none.
