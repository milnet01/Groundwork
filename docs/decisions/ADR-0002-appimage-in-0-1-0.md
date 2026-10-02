# ADR-0002: 0.1.0 ships as an AppImage

- **Status:** Accepted
- **Date:** 2026-10-02

## Context

At kickoff on 2026-09-25, packaging was put after 0.1.0, so 0.1.0 would
run from the source folder (`docs/brief.md`, "Decided at kickoff").
Groundwork is meant for fresh installs. Run from source, it needs a
compiler and the Qt 6 development packages installed first (ADR-0001).

## Decision

0.1.0 ships as an AppImage: one file that runs on a fresh install with
nothing installed first. RPM and OBS packaging still come after 0.1.0.
The user chose this on 2026-10-02, replacing the kickoff decision for
the AppImage only.

## Consequences

- The 0.1.0 release includes building, and testing, the AppImage.
- Anything the AppImage needs at run time is bundled in it or is part
  of a base openSUSE install.
- Nothing in the app may depend on files installed system-wide by a
  package, such as a polkit policy, because an AppImage installs none.
