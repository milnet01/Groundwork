#!/bin/bash
# Runs inside a throwaway container: a normal user with passwordless sudo
# runs the AppImage's Worker for real, then check mode, then looks at the
# video decoders.
set -u
# The desktop libraries an AppImage leaves to the system (X11, OpenGL,
# fonts): a desktop install has them, a bare container does not. Named by
# what they provide, as `ldd` reported them missing.
zypper -n --quiet install --no-recommends sudo \
  'libxcb.so.1()(64bit)' 'libX11.so.6()(64bit)' 'libX11-xcb.so.1()(64bit)' \
  'libOpenGL.so.0()(64bit)' 'libGLX.so.0()(64bit)' 'libEGL.so.1()(64bit)' \
  'libharfbuzz.so.0()(64bit)' 'libfreetype.so.6()(64bit)' 'libfontconfig.so.1()(64bit)' \
  'libSM.so.6()(64bit)' 'libICE.so.6()(64bit)' >/dev/null || { echo "setup failed"; exit 1; }
useradd -m tester
printf 'Defaults:tester verifypw=any\ntester ALL=(ALL) NOPASSWD: ALL\n' > /etc/sudoers.d/tester
chmod 0440 /etc/sudoers.d/tester
run() { su - tester -c "cd /tmp && APPIMAGE_EXTRACT_AND_RUN=1 /app/Groundwork-x86_64.AppImage $*"; }
echo "=== check before"; run --check
echo "=== worker"; run --worker system-update media-codecs flathub; echo "worker exit=$?"
echo "=== check after"; run --check
echo "=== decoders"; command -v ffmpeg && ffmpeg -hide_banner -decoders 2>/dev/null | grep -i -E ' h264 | hevc ' 
rpm -qa --qf '%{NAME}\t%{VENDOR}\n' 'libavcodec*'
