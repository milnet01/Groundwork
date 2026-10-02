#!/usr/bin/env bash
# Runs the built AppImage's Worker FOR REAL in a throwaway container of a
# supported system: a normal user with passwordless sudo applies the
# system update, the media codecs and Flathub, then check mode and the
# video decoders are read. Nothing touches this machine.
#
# Usage: tests/realrun/run-in-container.sh IMAGE
#   e.g. registry.opensuse.org/opensuse/tumbleweed:latest
#        registry.opensuse.org/opensuse/leap:16.0
# Needs build/appimage/Groundwork-x86_64.AppImage
# (packaging/appimage/build-appimage.sh). Prints the run's log.
set -Eeuo pipefail
cd "$(dirname "$0")/../.."
image=${1:?usage: $0 IMAGE}
[[ -x build/appimage/Groundwork-x86_64.AppImage ]] || { echo "build the AppImage first" >&2; exit 1; }
timeout 3000 podman run --rm \
    -v "$PWD/build/appimage:/app:ro,z" \
    -v "$PWD/tests/realrun:/run-scripts:ro,z" \
    "$image" bash /run-scripts/inside.sh
