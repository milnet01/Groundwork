# ADR-0001: Groundwork is written in C++ with Qt 6

- **Status:** Accepted
- **Date:** 2026-10-02

## Context

Groundwork needs a real graphical window, and it must run on a fresh
openSUSE install. `firewalld` requires `python3`, so a system with the
firewall has Python. A Python window would also need PySide6, installed
by the user or bundled in the app. The sibling app OneUp is Python and
PySide6, so writing Groundwork in Python would let it copy OneUp's code.

## Decision

Groundwork is written in C++ with Qt 6, for the window and for the part
that does root work. The user chose this on 2026-10-02, over the
recommended Python and PySide6, because they prefer C and C++.

## Consequences

- OneUp's code is not copied. Its lessons and its marker format are
  reused; its modules are re-implemented in C++.
- A build step exists from the start: a compiler, CMake and the Qt 6
  development packages.
- The program does not depend on any Python package.
- The AppImage carries Qt's libraries, so it does not rely on the
  desktop having Qt installed (ADR-0002).
