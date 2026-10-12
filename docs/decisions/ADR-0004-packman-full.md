# ADR-0004: Groundwork uses the full Packman repository

- **Status:** Accepted
- **Date:** 2026-10-12

## Context

ADR-0003 limited Groundwork to Packman's Essentials repository. That
held back GRND-0062: the Mesa video driver that decodes H.264 and HEVC
on AMD graphics is only in the full repository.

The user ruled on 2026-10-12 that a repository rule applies to
everything: where the software is in another repository, use it.

`opi codecs` (opi 5.16.0, `opi/__init__.py`, `add_packman_repo`) adds
the full repository and runs `zypper dup --from packman
--allow-downgrade --allow-vendor-change`. The owner's machine runs that
setup: its Mesa packages are Packman's builds.

## Decision

The codecs item adds the full Packman repository where none is
configured, and switches the system to Packman's builds with opi's
`zypper dup` command. Every item that installs from Packman installs
from the full repository. An Essentials repository already configured
is left in place. This supersedes ADR-0003.

## Consequences

- Video and audio programs, and drivers such as Mesa's, get Packman's
  builds, with the formats openSUSE leaves out.
- More system packages come from Packman, so a later update can stall
  when Packman lags behind openSUSE. openSUSE's wiki warns of this.
- Packman builds become the system's for good: zypper keeps a package's
  vendor, so later updates follow Packman's builds.
- The codecs item counts as done only with the full repository, so a
  system set up under ADR-0003 reads "not done" until it is switched.
