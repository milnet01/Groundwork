#
# spec file for package groundwork
#
# Copyright (c) 2026 Anthony Schemel
#
# All modifications and additions to the file contributed by third parties
# remain the property of their copyright owners, unless otherwise agreed
# upon. The license for this file, and modifications and additions to the
# file, is the same license as for the pristine package itself (unless the
# license for the pristine package is not an Open Source License, in which
# case the license is the MIT License). An "Open Source License" is a
# license that conforms to the Open Source Definition (Version 1.9)
# published by the Open Source Initiative.
#
# Please submit bugfixes or comments via
#   https://github.com/milnet01/Groundwork/issues
#

# Built on OBS in home:milnet:groundwork for Tumbleweed, Slowroll and
# Leap 16 (GRND-0031); packaging/obs/ drives it. Version: moves with
# CMakeLists.txt, through .claude/bump.json, and obs-submit.sh refuses a
# spec whose Version: differs from the tag _service pins.
#
# Every macro reference in a comment is written doubled (%%check). rpm
# expands macros inside comments, and a single one can turn into shell.

Name:           groundwork
Version:        0.5.2
Release:        0
Summary:        First-run setup wizard for openSUSE
License:        MIT
URL:            https://github.com/milnet01/Groundwork
Source0:        %{name}-%{version}.tar.gz
Source1:        %{name}-rpmlintrc
BuildRequires:  cmake >= 3.21
BuildRequires:  gcc-c++
BuildRequires:  cmake(Qt6Core)
BuildRequires:  cmake(Qt6LinguistTools)
BuildRequires:  cmake(Qt6Test)
BuildRequires:  cmake(Qt6Widgets)
# Owns /usr/share/icons/hicolor/scalable/apps. OBS checks directory
# ownership in the build root, where a Requires is not installed.
BuildRequires:  hicolor-icon-theme
Requires:       hicolor-icon-theme
# The Worker gets root through sudo -A (docs/design.md, Root).
Requires:       sudo
# Without it, Plasma on Wayland runs the window through XWayland (GRND-0045).
Recommends:     qt6-wayland

%description
Groundwork sets up a fresh openSUSE install in one window. It lists what
a new install usually lacks: media codecs, Flathub, graphics drivers,
better fonts and more. Each item has a switch, a plain explanation and a
status, and checks itself first, so running it again is safe. It asks
for a password once, when it applies the choices. It works on
Tumbleweed, Slowroll and Leap 16 or later.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -DGROUNDWORK_TESTS=ON
%cmake_build

%install
%cmake_install

%check
# The password-box test and the translation tests open windows; a build
# machine has no display.
export QT_QPA_PLATFORM=offscreen
%ctest

%files
%license LICENSE
%doc README.md CHANGELOG.md
%{_bindir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg

%changelog
