# GRND-0076 — Prepare a drive: loop log

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|---|---|---|---|---|---|---|---|
| impl | 2026-10-15 | none — implementation, no reviewer dispatched | 0 | 0 | 0 | 0 | § 4.1's command gains `RO`: "not read-only" needs that column, which the command did not ask for. |
| impl | 2026-10-29 | none — correction from review-code lane 03, no reviewer dispatched | 0 | 0 | 0 | 0 | § 4.1's `START` reads as 512-byte units, not logical sectors, matching the code and its test; the old reading misplaced partitions on 4096-byte drives. |
