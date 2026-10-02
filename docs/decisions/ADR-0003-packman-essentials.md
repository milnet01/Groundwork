# ADR-0003: Codecs come from Packman's Essentials repository

- **Status:** Accepted
- **Date:** 2026-10-02

## Context

Video and audio need codecs from Packman. Two current sources disagree on
how to add them.

- openSUSE's wiki page `SDB:Installing_codecs_from_Packman_repositories`
  recommends the Essentials repository only. Its banner reads: "Packman
  is a frequent source of issues with updates. If possible, only add the
  essentials repository." Read from a web.archive.org snapshot of
  2026-05-11; the live page refused automated reads.
- `opi codecs` (opi 5.16.0) adds the full Packman repository and switches
  system packages to it (`opi/plugins/packman.py`).

The full repository swaps more of the system to Packman's builds, so
more of each later update depends on Packman.

## Decision

The codecs item adds Packman's Essentials repository, not the full one,
and installs the codec packages from it, following the wiki. The exact
commands are taken from the wiki when the item is built.

## Consequences

- Fewer system packages change vendor, so later updates have fewer
  Packman conflicts.
- Programs that need the full Packman repository are not covered. A user
  who wants it adds it themselves.
- The item must recognise a system that already has the full Packman
  repository, and report codecs as done, rather than adding Essentials
  beside it.
