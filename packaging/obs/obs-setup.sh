#!/usr/bin/env bash
# Creates or updates the OBS subproject home:milnet:groundwork and its
# package (GRND-0031). Safe to re-run: it re-applies the settings below,
# so it is also how a build target is added or removed.
#
# Usage: packaging/obs/obs-setup.sh
# Needs osc, logged in to OBS. Then run packaging/obs/obs-submit.sh.
#
# The targets are the three systems Groundwork supports. home:milnet and
# home:milnet:ants-terminal build no Slowroll tree, so this subproject
# has its own (checked 2026-10-13).
#
# <debuginfo> makes OBS strip the program and put its symbols in a
# separate -debuginfo package; without it the program ships unstripped.
# openSUSE:Factory sets the same flag. OBS rejects a double hyphen inside
# an XML comment, so explanations live here, not in the XML.
set -Eeuo pipefail

API=${OBS_API:-https://api.opensuse.org}
PROJ=${OBS_PROJECT:-home:milnet:groundwork}
PKG=${OBS_PACKAGE:-groundwork}
OWNER=${OBS_USER:-milnet}

command -v osc >/dev/null || { echo "obs-setup: osc is needed" >&2; exit 1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cat >"$tmp/prj.xml" <<EOF
<project name="$PROJ">
  <title>Groundwork</title>
  <description>First-run setup wizard for openSUSE: media codecs, Flathub, drivers and more, in one window.</description>
  <person userid="$OWNER" role="maintainer"/>
  <debuginfo>
    <enable/>
  </debuginfo>
  <repository name="openSUSE_Tumbleweed">
    <path project="openSUSE:Factory" repository="snapshot"/>
    <arch>x86_64</arch>
  </repository>
  <repository name="openSUSE_Slowroll">
    <path project="openSUSE:Slowroll" repository="standard"/>
    <arch>x86_64</arch>
  </repository>
  <repository name="openSUSE_Leap_16.0">
    <path project="openSUSE:Leap:16.0" repository="standard"/>
    <arch>x86_64</arch>
  </repository>
</project>
EOF
echo "obs-setup: applying project settings: $PROJ"
osc -A "$API" meta prj "$PROJ" -F "$tmp/prj.xml"

cat >"$tmp/pkg.xml" <<EOF
<package name="$PKG" project="$PROJ">
  <title>First-run setup wizard for openSUSE</title>
  <description>Groundwork, built from its release tag on GitHub.</description>
  <person userid="$OWNER" role="maintainer"/>
</package>
EOF
echo "obs-setup: applying package settings: $PROJ/$PKG"
osc -A "$API" meta pkg "$PROJ" "$PKG" -F "$tmp/pkg.xml"

echo "obs-setup: $PROJ/$PKG is ready. Next: packaging/obs/obs-submit.sh"
