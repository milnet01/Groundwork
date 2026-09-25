# Groundwork — instructions for Claude Code

## Where this project is

**State:** 1 — Unstated. Nothing has been written about what this is for.
**In flight:** nothing.

> Keep the two lines above true, and keep them to two lines. They are
> the only position this project records. Everything else about where
> work stands is read off things that cannot lie — whether a spec exists,
> whether tests fail, what `git status` says, whether the roadmap bullet
> is 🚧. A recorded step number starts lying the first time a session
> forgets to update it, and still reads as authoritative.
>
> **There is deliberately no `Next:` line.** The next action follows from
> the state — `~/.claude/workflow.md` names what each state leads to —
> and once the roadmap has items, from the roadmap. A hand-kept forward
> pointer is the exact shape the paragraph above rejects, and it goes
> stale first because nothing contradicts it when it does.

## How work is done here

- **`~/.claude/workflow.md`** — the states, the gates, and what "done"
  means. Read in place. This project does not have its own copy.
- **`~/.claude/standards/`** — how to write code, tests, commits,
  documents, releases. Also read in place.

Neither is summarised here. A rule restated in two places is two rules
that will disagree.

## Ants MCP

Prefer an Ants MCP verb over a raw scan, read, or edit wherever one fits.  `tool_info {catalog:true}` returns the whole verb list in one call — reach for it rather than guessing a verb name.

**Case matters.** Roadmap and changelog IDs are matched case-sensitively; a roadmap section slug in the wrong case refuses `bad_case` and hands back `canonical_slug`. Copy identifiers, don't retype them.

**Project layout.** If `<root>/.ants/project.json` is missing or incomplete, declare it so the verbs stop guessing. Use `project_settings` — never hand-write the JSON, because the verb validates that every path exists (directories as directories, files as files) and rejects the write otherwise.

- `op:"get"` — what the project declares now, and what is still unset.  This is the orientation read.
- `op:"detect"` — what it *should* declare, proposed from conventional paths for all six keys: `source_roots[]`, `test_roots[]`, `docs_dir`, `specs_dir`, `roadmap`, `changelog`.
- `op:"init"` — accept the proposal. `op:"set"` — fill in whatever is left.

Read the reply rather than skimming it. `undeclared[]` is a to-do list — a candidate path exists on disk, so `set` will take it. `unavailable[]` is not: there is nothing to point at. `declared_missing[]` names a declared path that no longer resolves; the reader drops those silently, so from the outside they are indistinguishable from never having been declared. A reason of "no source_roots override needed" is about `source_roots` alone, not a verdict on the other five. The file is world-readable — no secrets in it.

## Ants MCP feedback

Findings about Ants MCP itself go in
`/mnt/Games/Scripts/Linux/Ants_MCP_Feedback_Files/<project>_Ants_MCP_Feedback.md`,
written through the verbs — not with Edit, Write, or a shell redirect.

- `feedback_query` reads the un-triaged tail instead of the whole file.  `mapped_id_status` resolves each assigned roadmap ID against the live ROADMAP, so it tells you which of your earlier asks have shipped; `include_tracking:true` adds the maintainer's mapping rows.
- `feedback_log op:"append_finding"` adds a finding. It always appends at EOF and never writes above a maintainer block.
- Omitting `path` derives `<caller_cwd leaf>_Ants_MCP_Feedback.md` in that directory. Pass `path` explicitly when the filename you want differs from the directory's leaf name.
- The maintainer ops — `append_tracking`, `assign_id`, `compact_*`, `prune_tracking`, `migrate_v2` — belong to the session that owns Ants MCP.  Don't run them, and never rewrite the roadmap IDs it has annotated.  Confirm a shipped fix by appending, not by editing its block.

## Working principles

- **Think before coding.** No silent assumptions — state one in chat rather than burying it in a diff.
- **Simplicity first.** No over-engineering.
- **Surgical changes.** Every changed line traces to the work you chose.
- **Goal-driven.** Explicit verification; evidence before claims.

## Tokens

Be token-aware without buying it with being wrong. Prefer an MCP verb to a raw scan, a targeted read to a whole file, and a subagent where the fan-out would otherwise land in this context. Thoroughness that costs tokens is fine; volume that buys nothing is not. If you drop something for cost, say which and why — a quiet omission reads as coverage.

## Writing and editing documents

- **No counts, line numbers, or sizes.** They go stale the moment anything changes. Name the thing instead — the section, the heading, the filename, the symbol.
- **Keep prose short.** One claim per sentence. Cut throat-clearing and hedging. Long prose hides contradictions; short prose gives them fewer places to hide.
- **Every statement must be checkable.** If you can't point at the file, command, or output that backs it, don't write it — say it's unverified rather than guessing.

## This project's own facts

Everything below is specific to this project, which is why it lives here
rather than in a standard.

### Stack

(Decided in design — `docs/design.md`. Until then, undecided.)

### Build and test

The CI gate is `scripts/local-ci.sh`. GitHub's `ci.yml` calls it and adds
no steps of its own, so a change to CI goes in the script. It has two modes:

- `./scripts/local-ci.sh` — the full gate. Build and test legs are empty
  until the stack exists.
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

### Roadmap IDs

`GRND-NNNN`, per `roadmap-format.md` § 3.5.1. Commit subjects
are `<ID>: <description>`, per `commits.md`.

### Overrides

Any place this project deliberately departs from a global standard goes
in `docs/standards/`, with the reason. If that directory is empty, there
are none.
