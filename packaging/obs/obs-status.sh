#!/usr/bin/env bash
# Waits until every OBS target has built the package's current source
# revision, then says how each ended (GRND-0031). A build passes only if
# its log shows the test suite ran.
#
# Usage: packaging/obs/obs-status.sh [--rev N]
# Exit: 0 every target built and tested; 1 one failed or cannot build;
#       2 still building when the polls ran out; 3 OBS could not be read.
#
# It reads each target's job history rather than its status word. Right
# after a commit every target still says "succeeded", for the previous
# revision; a job-history row is written when a build ends and names the
# revision it built. Ants Terminal learned this the hard way (ANTS-5577).
set -Eeuo pipefail

API=${OBS_API:-https://api.opensuse.org}
PROJ=${OBS_PROJECT:-home:milnet:groundwork}
PKG=${OBS_PACKAGE:-groundwork}
POLL_SECS=${POLL_SECS:-60}
MAX_POLLS=${MAX_POLLS:-90}
STUCK_POLLS=${STUCK_POLLS:-10}

REV=""
case ${1:-} in
    --rev) REV=${2:?usage: obs-status.sh [--rev N]} ;;
    '') ;;
    *) echo "usage: obs-status.sh [--rev N]" >&2; exit 2 ;;
esac
command -v osc >/dev/null || { echo "obs-status: osc is needed" >&2; exit 1; }

# One line per target: "<repository> <arch> <status>".
results() {
    osc -A "$API" api "/build/$PROJ/_result?package=$PKG" 2>/dev/null | awk '
        /<result / {
            repo = ""; arch = ""
            if (match($0, / repository="[^"]*"/)) repo = substr($0, RSTART + 13, RLENGTH - 14)
            if (match($0, / arch="[^"]*"/)) arch = substr($0, RSTART + 7, RLENGTH - 8)
        }
        /<status / { if (match($0, / code="[^"]*"/)) print repo, arch, substr($0, RSTART + 7, RLENGTH - 8) }'
}

# How the newest finished build of revision REV or later ended, or
# nothing if none has finished. "Or later" because a service run can add
# a revision on top of ours, and its build then stands for ours.
job_code() {
    osc -A "$API" api "/build/$PROJ/$1/$2/_jobhistory?package=$PKG&limit=5" 2>/dev/null | awk -v want="$REV" '
        /<jobhist / {
            rev = ""; code = ""
            if (match($0, / rev="[0-9]+"/)) rev = substr($0, RSTART + 6, RLENGTH - 7)
            if (match($0, / code="[^"]*"/)) code = substr($0, RSTART + 7, RLENGTH - 8)
            if (rev != "" && rev + 0 >= want + 0) last = code
        }
        END { if (last != "") print last }'
}

# The last finished build's log. nostream=1 returns what the server has
# instead of following a running build forever.
build_log() {
    timeout 300 osc -A "$API" api "/build/$PROJ/$1/$2/$PKG/_log?nostream=1&last=1" 2>/dev/null || true
}

if [[ -z $REV ]]; then
    REV=$(osc -A "$API" api "/source/$PROJ/$PKG" 2>/dev/null |
        sed -n 's/^<directory[^>]* rev="\([0-9][0-9]*\)".*/\1/p' | head -n 1)
fi
[[ $REV =~ ^[0-9]+$ ]] || { echo "obs-status: cannot read the source revision of $PROJ/$PKG" >&2; exit 3; }
echo "obs-status: waiting for $PROJ/$PKG revision $REV"

poll=0 stuck=0 res=""
while :; do
    poll=$((poll + 1))
    res=$(results) || res=""
    pending=0 sawstuck=0 line=""
    [[ -n $res ]] || pending=1
    while read -r repo arch status; do
        [[ -n ${status:-} ]] || continue
        [[ $status == excluded || $status == disabled ]] && continue
        code=$(job_code "$repo" "$arch")
        if [[ -n $code ]]; then
            line+=" $repo=$code"
            continue
        fi
        case $status in
            unresolvable | broken)
                # Either can pass while the source service runs, so it is
                # final only once it has lasted STUCK_POLLS.
                sawstuck=1
                line+=" $repo=$status"
                ((stuck >= STUCK_POLLS)) || pending=1 ;;
            *)
                pending=1
                line+=" $repo=waiting($status)" ;;
        esac
    done <<<"$res"
    echo "[poll $poll]${line:- results not readable}"
    if ((sawstuck)); then stuck=$((stuck + 1)); else stuck=0; fi
    ((pending == 0 || poll >= MAX_POLLS)) && break
    sleep "$POLL_SECS"
done

[[ -n $res ]] || { echo "obs-status: cannot read the build results of $PROJ/$PKG" >&2; exit 3; }

echo
echo "=== revision $REV ==="
rc=0
while read -r repo arch status; do
    [[ -n ${status:-} ]] || continue
    [[ $status == excluded || $status == disabled ]] && continue
    code=$(job_code "$repo" "$arch")
    case $code in
        succeeded | unchanged)
            summary=$(build_log "$repo" "$arch" | grep -E '[0-9]+% tests passed' | tail -n 1 || true)
            if [[ -n $summary ]]; then
                echo "$repo: $code; $summary"
            else
                echo "$repo: $code, but its log shows no test run"
                rc=1
            fi ;;
        '')
            if [[ $status == unresolvable || $status == broken ]]; then
                echo "$repo: $status, so it cannot build. Why: osc -A $API results -v $PROJ $PKG"
                rc=1
            else
                echo "$repo: no finished build of revision $REV after $poll polls (now: $status)"
                ((rc != 0)) || rc=2
            fi ;;
        *)
            rc=1
            echo
            echo "=== $repo: $code; the end of its log ==="
            build_log "$repo" "$arch" | tail -n 40 ;;
    esac
done <<<"$res"
exit "$rc"
