#!/usr/bin/env bash
# Sends the release named in packaging/obs/_service to OBS, which then
# builds it for every target (GRND-0031). Run it after the release tag is
# on GitHub, then wait with packaging/obs/obs-status.sh.
#
# Usage: packaging/obs/obs-submit.sh
# Needs osc, logged in to OBS, and packaging/obs/obs-setup.sh run once.
#
# It copies _service, the spec and its rpmlintrc into an osc checkout
# under build/obs, adds a .changes entry for a new version, and commits. Nothing large is uploaded: OBS clones the tag itself.
# The spec lives in packaging/opensuse/ and is copied, never edited in
# the checkout, so OBS cannot hold a different spec from the repository.
set -Eeuo pipefail
cd "$(dirname "$0")/../.."

API=${OBS_API:-https://api.opensuse.org}
PROJ=${OBS_PROJECT:-home:milnet:groundwork}
PKG=${OBS_PACKAGE:-groundwork}
SERVICE=packaging/obs/_service
SPEC=packaging/opensuse/groundwork.spec

fail() { echo "obs-submit: $*" >&2; exit 1; }
command -v osc >/dev/null || fail "osc is needed"

tag=$(sed -n 's|.*<param name="revision">\(.*\)</param>.*|\1|p' "$SERVICE")
url=$(sed -n 's|.*<param name="url">\(.*\)</param>.*|\1|p' "$SERVICE")
version=$(sed -n 's/^Version:[[:space:]]*//p' "$SPEC")
[[ -n $tag && -n $url ]] || fail "$SERVICE names no <revision> or <url>"

# OBS makes the tarball's version from the tag; a spec naming another
# version asks for a Source0 that never exists, and fails at the fetch.
[[ $tag == "v$version" ]] ||
    fail "$SERVICE pins $tag but $SPEC says Version: $version. They must match."

# OBS clones from GitHub, so a tag that is only local fails the build.
git ls-remote --exit-code --tags "$url" "refs/tags/$tag" >/dev/null ||
    fail "tag $tag is not on $url. Push it first."

# rpm expands macros inside # comments, so a single % there can become
# shell and break the build minutes in. Doubled (%%) is safe.
bad=$(awk '/^[[:space:]]*#/ { l = $0; gsub(/%%/, "", l); if (l ~ /%[A-Za-z_{]/) print NR": "$0 }' "$SPEC")
[[ -z $bad ]] || fail "a single % in a comment in $SPEC; write it %%:"$'\n'"$bad"
if command -v rpmspec >/dev/null; then
    rpmspec -P "$SPEC" >/dev/null || fail "rpmspec cannot read $SPEC"
fi

workdir=build/obs
checkout=$workdir/$PROJ/$PKG
mkdir -p "$workdir"
if [[ -d $checkout/.osc ]]; then
    (cd "$checkout" && osc -A "$API" update)
else
    (cd "$workdir" && osc -A "$API" checkout "$PROJ" "$PKG")
fi
cp "$SERVICE" "$checkout/_service"
cp "$SPEC" "$checkout/groundwork.spec"
cp packaging/opensuse/groundwork-rpmlintrc "$checkout/"

cd "$checkout"
# openSUSE wants a .changes entry per version. It lives only on OBS:
# osc vc signs it with the OBS account's name and email, which stay out
# of this repository. The full notes are on the GitHub release.
if ! grep -qF -- "- Update to $version" groundwork.changes 2>/dev/null; then
    osc -A "$API" vc -m "Update to $version. Release notes: ${url%.git}/releases/tag/$tag" groundwork.changes
fi
osc -A "$API" addremove
# A commit can fail and pass on a second try: OBS answers 503 while busy,
# and a tag push can add a package revision between update and commit.
tries=0
until osc -A "$API" commit -m "groundwork $tag"; do
    tries=$((tries + 1))
    ((tries < 5)) || fail "the commit failed $tries times. It is safe to run this again."
    echo "obs-submit: the commit failed; trying again ($tries)" >&2
    sleep 30
    osc -A "$API" update || true
done
echo "obs-submit: $tag committed. Wait for the builds: packaging/obs/obs-status.sh"
