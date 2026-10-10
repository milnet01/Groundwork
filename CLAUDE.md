# Groundwork — instructions for Claude Code

## Where this project is

**State:** 5 — Building an item.
**In flight:** 0.3.0's items, GRND-0053 next; GRND-0033 waits for a fluent reader.

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

Each language has `translations/groundwork_<code>.ts`, compiled into the
program at `:/i18n`. A language is offered only once its file has
translations. After changing any text a user sees, refresh the files:

```bash
cmake --build build --target update_translations
```

`tst_translations` fails until they match the source.

The trap: `lupdate` files a string under a context only from what it can
see at the call. A local `tr` helper in a free function gets the wrong
context, or none, so that text is never translated. Inside a free
function, name the context at the call:
`QCoreApplication::translate("gw::Name", "...")`. `tst_translations`
checks every context the code looks up. Two more of the same kind: a
count must be passed at that named call, or the string gets no plural
forms; and after a named call inside `?:`, `lupdate` filed the other
branch's `tr()` under `QCoreApplication`, so give each its own
statement (`updateitem.cpp`).

Right-to-left text has a trap of its own. Qt sets a label's direction
from its first letter. A translation that starts with a Latin name or a
`%1` lays out left to right, and its full stop lands at the wrong end.
Start such a translation with U+200F, the right-to-left mark. Put U+200E,
the left-to-right mark, after "C++" and before ".7z", or their symbols
land on the wrong side. Only a look at the window shows either fault.

A translation checked only by a machine (Claude, Gemini) is still a
draft (design.md, Text). A language leaves draft by adding its code to
`kChecked` in `src/core/translations.cpp`.

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
