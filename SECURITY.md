# Security policy — Groundwork

> **Delete this file if this project has no trust boundary.** A local
> single-user tool that reads nothing it did not write and talks to
> nothing may genuinely have none, and an empty policy is worse than no
> file — it claims a promise nobody is keeping.

`~/.claude/standards/security.md` owns what a trust boundary is and what
defending one requires. This file is only what an outside reader needs.

## Trust boundaries

> Where data crosses from somewhere less trusted to somewhere more
> trusted: user input, files, the network, plugins, another process. One
> line each — what crosses, and what is checked at the crossing.

- **A layout file, read by the Worker before it partitions a drive as
  root.** The Worker trusts none of it and validates it again itself.
  Checks: `docs/specs/GRND-0076-prepare-a-drive.md` INV-3, INV-4, INV-8.
- **A saved setup file, from a stick that may have been edited.** Its
  sources reach `zypper` as root, and its settings are written into the
  home folder. Checks: `docs/specs/GRND-0078-saved-setup-file.md` INV-3,
  INV-10; `docs/specs/GRND-0079-start-from-saved-setup.md` INV-2, INV-5,
  INV-8.
- **The choices file the wizard writes for the Worker.** Its settings
  paths are checked before any `tar` call:
  `docs/specs/GRND-0078-saved-setup-file.md` INV-3.

## Supported versions

> Which versions get security fixes. Before 1.0, usually just the latest.

## Reporting a vulnerability

> How to reach someone privately, and what to expect. A public issue
> tracker is the wrong channel and saying so here is the point of the
> section.
