#!/usr/bin/env bash
# Builds Groundwork-x86_64.AppImage inside a Leap 16 container, the oldest
# supported system, so the bundled libraries are no newer than any
# supported system's (docs/design.md, The stack; ADR-0002).
#
# Usage: packaging/appimage/build-appimage.sh
# Output: build/appimage/Groundwork-x86_64.AppImage
set -Eeuo pipefail
cd "$(dirname "$0")/../.."

# shellcheck source=packaging/appimage/tools.env
. packaging/appimage/tools.env

command -v podman >/dev/null || { echo "build-appimage: podman is needed" >&2; exit 1; }
mkdir -p build/appimage build/appimage-tools

# Download each tool once, and refuse one whose digest does not match.
fetch() { # owner/repo, release, file, sha256
    local path="build/appimage-tools/$3"
    if [[ ! -f $path ]] || ! echo "$4  $path" | sha256sum -c --quiet - 2>/dev/null; then
        curl -fsSL -o "$path" "https://github.com/$1/releases/download/$2/$3"
    fi
    echo "$4  $path" | sha256sum -c --quiet -
    chmod +x "$path"
}
fetch linuxdeploy/linuxdeploy "$LINUXDEPLOY_RELEASE" linuxdeploy-x86_64.AppImage "$LINUXDEPLOY_SHA256"
fetch linuxdeploy/linuxdeploy-plugin-qt "$LINUXDEPLOY_QT_RELEASE" linuxdeploy-plugin-qt-x86_64.AppImage "$LINUXDEPLOY_QT_SHA256"
fetch AppImage/type2-runtime "$RUNTIME_RELEASE" runtime-x86_64 "$RUNTIME_SHA256"

# The source is mounted read-only; the build happens in the container. A
# stalled step fails after 25 minutes rather than hanging. The script is
# single-quoted on purpose: its variables expand inside the container.
# shellcheck disable=SC2016
timeout 1500 podman run --rm \
    -v "$PWD:/src:ro,z" \
    -v "$PWD/build/appimage:/out:z" \
    -v "$PWD/build/appimage-tools:/tools:ro,z" \
    "$BUILD_IMAGE" bash -Eeuo pipefail -c '
        zypper -n --quiet install --no-recommends cmake ninja gcc-c++ \
            qt6-base-devel qt6-base-common-devel qt6-widgets-devel \
            qt6-linguist-devel qt6-wayland \
            file findutils gzip >/dev/null
        cmake -S /src -B /tmp/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DGROUNDWORK_TESTS=OFF -DCMAKE_INSTALL_PREFIX=/usr >/dev/null
        cmake --build /tmp/build --parallel 4
        DESTDIR=/tmp/AppDir cmake --install /tmp/build >/dev/null
        cd /out
        export APPIMAGE_EXTRACT_AND_RUN=1 QMAKE=/usr/bin/qmake6 \
            LDAI_RUNTIME_FILE=/tools/runtime-x86_64 LDAI_NO_APPSTREAM=1 \
            EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so" \
            EXTRA_QT_MODULES=waylandcompositor
        cp /tools/linuxdeploy-plugin-qt-x86_64.AppImage /tmp/
        PATH=/tmp:$PATH /tools/linuxdeploy-x86_64.AppImage --appdir /tmp/AppDir \
            --desktop-file /tmp/AppDir/usr/share/applications/groundwork.desktop \
            --icon-file /tmp/AppDir/usr/share/icons/hicolor/scalable/apps/groundwork.svg \
            --plugin qt --output appimage
        test -f Groundwork-x86_64.AppImage
        # Without it, Plasma on Wayland runs the app through XWayland (GRND-0045).
        test -f /tmp/AppDir/usr/plugins/platforms/libqwayland-generic.so
        test -f /tmp/AppDir/usr/plugins/wayland-shell-integration/libxdg-shell.so
        ls /tmp/AppDir/usr/plugins
    '
echo "build-appimage: build/appimage/Groundwork-x86_64.AppImage"
