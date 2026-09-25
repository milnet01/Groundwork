#!/usr/bin/env bash
# THE CI GATE. One list of steps, run in two places.
#
# `.github/workflows/ci.yml` CALLS this file and restates none of it. That is
# the whole point (`local-gate.md` § 3): two lists of steps drift the first
# time someone edits one of them, and the drift surfaces as a red build on a
# push — which is exactly what running locally was meant to prevent.
#
# So: to change what CI does, change THIS FILE. Never add a step to the
# workflow. Tool VERSIONS live in scripts/ci-tools.env, which the workflow
# installs from and this script checks against.
#
# Usage:
#   ./scripts/local-ci.sh              the full gate
#   ./scripts/local-ci.sh --docs       the checks that do not need a build
#   ./scripts/local-ci.sh --docs-glob  print what counts as documentation
#
# Every push runs this through .githooks/pre-push, which hands off to
# ~/.claude/githooks/pre-push. That hook adds --docs when every changed path
# matches DOCS_GLOB. Per clone, once:
#   git config ants.gate.command  ./scripts/local-ci.sh
#   git config ants.gate.docsMode --docs
#   git config ants.gate.docsGlob "$(./scripts/local-ci.sh --docs-glob)"
#
# WHY --docs EXISTS, AND WHY IT IS NOT A SKIP. Rebuilding and re-testing to fix
# a prose typo is how a person learns to reach for --no-verify. So --docs omits
# only the legs that need a compiler or a full environment, and NOTHING ELSE.
# GitHub always runs the full gate (ci.yml passes no flag), so every check cheap
# enough to run on a typo runs in both modes; otherwise a local green would mean
# less than the green GitHub then applies.
set -Eeuo pipefail
cd "$(dirname "$0")/.."

# The one definition of what counts as documentation. The pre-push hook reads
# it with `--docs-glob` and does its own matching, so the decision has one home
# as well as one value.
DOCS_GLOB='docs/*|*.md|LICENSE'

if [[ ${1:-} == --docs-glob ]]; then printf '%s\n' "$DOCS_GLOB"; exit 0; fi

DOCS_ONLY=0
case ${1:-} in
    '') ;;
    --docs) DOCS_ONLY=1 ;;
    *) printf 'usage: %s [--docs | --docs-glob]\n' "$0" >&2; exit 2 ;;
esac

# shellcheck source=scripts/ci-tools.env
. scripts/ci-tools.env

step() { printf '\n=== %s ===\n' "$1" >&2; }
fail() { printf 'local-ci: %s\n' "$1" >&2; exit 1; }

# A check that did not run must not look like one that passed. Locally a
# missing or different-version tool is reported and the run goes on, so you can
# still test your change. On GitHub (CI=true) the workflow installed the pinned
# tools, so any gap there is fatal.
PROBLEMS=()
problem() {
    printf 'local-ci: %s\n' "$1" >&2
    [[ ${CI:-} == true ]] && exit 1
    PROBLEMS+=("$1")
}
need() {
    command -v "$1" >/dev/null 2>&1 && return 0
    problem "$1 is not installed — that check did NOT run"
    return 1
}
# $1 tool, $2 pinned version, $3 version found. A leading `v` is ignored on
# both sides; builds of one release disagree about it.
check_version() {
    [[ ${2#v} == "${3#v}" ]] && return 0
    problem "$1 is $3 but scripts/ci-tools.env pins $2 — its answer may not match GitHub's"
}

# ── Checks that run in BOTH modes ───────────────────────────────────────────

step 'gate wiring'
# Before the --docs exit on purpose: the glob decides which mode runs, so a
# drifted copy could never be caught by the mode it wrongly selects.
configured=$(git config --get ants.gate.docsGlob 2>/dev/null || true)
if [[ -n $configured && $configured != "$DOCS_GLOB" ]]; then
    fail "ants.gate.docsGlob is '$configured' but this script says '$DOCS_GLOB'.
  re-run: git config ants.gate.docsGlob \"\$(./scripts/local-ci.sh --docs-glob)\""
fi
printf 'docs glob: %s\n' "$DOCS_GLOB" >&2

step 'documents present and readable'
for f in README.md CLAUDE.md ROADMAP.md CHANGELOG.md docs/brief.md \
         docs/discovery.md docs/design.md; do
    [[ -s $f ]] || fail "missing or empty: $f"
    iconv -f UTF-8 -t UTF-8 "$f" >/dev/null 2>&1 || fail "not valid UTF-8: $f"
done
printf 'core documents present and valid UTF-8\n' >&2

step 'relative links resolve'
# Every tracked markdown file, docs/ included — the pre-commit hook skips
# docs/, so this is the only place those links are checked.
broken=0
while IFS= read -r file; do
    dir=$(dirname "$file")
    while IFS= read -r target; do
        # Web links, other schemes and in-page anchors are not this check's.
        [[ $target =~ ^[a-zA-Z][a-zA-Z0-9+.-]*: || $target == \#* ]] && continue
        target=${target%%#*}   # the path must exist; the anchor is not checked
        target=${target%% *}   # drop a "path 'title'" suffix
        [[ -z $target ]] && continue
        resolved=$target
        [[ $target != /* ]] && resolved="$dir/$target"
        if [[ ! -e $resolved ]]; then
            printf '  %s -> %s does not exist\n' "$file" "$target" >&2
            broken=$((broken + 1))
        fi
    done < <(grep -oE '\]\([^)]+\)' "$file" | sed -E 's/^\]\(//; s/\)$//')
done < <(git ls-files '*.md')
(( broken == 0 )) || fail "$broken broken relative link(s)"
printf 'every relative link resolves\n' >&2

step 'shell scripts (shellcheck)'
# The git hooks too: they run on every commit and push.
mapfile -t SCRIPTS < <(git ls-files 'scripts/*.sh' '.githooks/*')
(( ${#SCRIPTS[@]} > 0 )) || fail "no tracked shell scripts — this is not the checkout this script belongs to"
if need shellcheck; then
    check_version shellcheck "$SHELLCHECK_VERSION" \
        "$(shellcheck --version | awk '/^version:/ {print $2}')"
    shellcheck -x "${SCRIPTS[@]}"
    printf 'shellcheck clean (%d files)\n' "${#SCRIPTS[@]}" >&2
fi

step 'workflows (actionlint)'
if need actionlint; then
    check_version actionlint "$ACTIONLINT_VERSION" "$(actionlint --version | head -n1)"
    actionlint
    printf 'actionlint clean\n' >&2
fi

report() {
    if (( ${#PROBLEMS[@]} )); then
        printf '\nlocal-ci: %s passed, but NOT everything GitHub checks ran as GitHub runs it:\n' "$1" >&2
        printf '  - %s\n' "${PROBLEMS[@]}" >&2
    else
        printf '\nlocal-ci: %s passed.\n' "$1" >&2
    fi
}

if (( DOCS_ONLY )); then
    report '--docs'
    printf 'local-ci: build and test legs skipped by design.\n' >&2
    exit 0
fi

# ── Checks that need a build or a full environment ──────────────────────────
# Empty until the stack is chosen (docs/design.md). Add each leg here, never in
# ci.yml, and pin its tool in scripts/ci-tools.env.

step 'lint'
step 'types'
step 'build'
step 'test'

report 'full gate'
