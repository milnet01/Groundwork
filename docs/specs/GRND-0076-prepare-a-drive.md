<!-- ants-spec-format: 1 -->
# GRND-0076 — Prepare a drive for installing openSUSE

**Status:** accepted (2026-10-15). No review gate: this project cancels it (CLAUDE.md, Reviews and specs).
**Kind:** feature.
**Source:** ROADMAP GRND-0076 (user request, 2026-10-15).

**Blocked by:** GRND-0077.

**Layman:** Groundwork can partition a new drive, or the free space
beside Windows, so the openSUSE installer finds it ready; nothing is
erased until you have seen the drive and switched on "I understand".

## 1. Goal

From the wizard's "prepare a drive" job, a user picks one drive, picks a
suggested layout or builds their own, confirms, and the Worker writes
the partitions and filesystems. Afterwards the wizard shows what to
pick in the installer. A drive in use cannot be picked, and the drive
written is the drive the user saw (discovery S5).

## 2. Problem

Groundwork has no way to read drives or write a partition table. The
design places this in a new part, Drives (`docs/design.md`, Preparing a
drive), and every root step goes through the Worker (`docs/design.md`,
What may depend on what). Three things make it expensive to get wrong:

1. Writing a partition table erases what was on the drive. Picking the
   wrong drive, or a drive that changed between picking and writing,
   cannot be undone.
2. The Worker today takes item ids only
   (`docs/reference/worker-interface.md`, Command line). A layout is
   new input it must accept, and must not trust.
3. Leap 16's installer, Agama, wipes the drive in its own proposal and
   reuses existing partitions only when told to, through a setting that
   is easy to miss
   (Source: https://forums.opensuse.org/t/how-to-install-16-0-while-keeping-the-existing-partition-scheme/194202).

## 3. Scope decisions (agreed with the user)

- Suggested layouts, plus a custom layout the user builds (user,
  2026-10-15).
- Where Windows is on the drive: a checklist first (back up, turn off
  Fast Startup, check for BitLocker, shrink Windows from Windows' own
  Disk Management); Groundwork never shrinks Windows; defragmenting is
  optional advice for spinning disks (user, 2026-10-15).
- The wipe is confirmed with a switch, not by typing the drive's name
  (user, 2026-10-15).
- The installer stays a full alternative; preparing is optional (user,
  2026-10-15).
- Beside Windows, openSUSE gets its own new start-up partition (user,
  2026-10-15).
- Room to hibernate is offered on every layout, with a warning where
  it takes a large share of the space (user, 2026-10-15). The 5 %
  threshold in § 4.3 is Claude's reading of "if relevant".

## 4. Design

### 4.1 Reading the drives

Drives reads one command, without root:

```
lsblk --json --bytes --output NAME,PATH,TYPE,SIZE,START,PTTYPE,PTUUID,PARTTYPE,FSTYPE,LABEL,PARTLABEL,MOUNTPOINTS,RM,ROTA,TRAN,MODEL,SERIAL,LOG-SEC
```

Measured 2026-10-15 on util-linux 2.42.4: every column is readable
without root, and `START` is in logical sectors. Through
`CheckContext::run`, with `LC_ALL=C` like every check.

A drive is a `TYPE` `disk` that is not read-only. It **cannot be
picked** when any device under it, at any depth (partitions, and the
`crypt` or `lvm` devices on them), has a mount point or is active swap
(`/proc/swaps`). That covers the running system, and the stick a live
system started from, which is mounted. The picker shows such a drive
greyed, saying why.

Each drive is shown by model, size in GB, connection (`TRAN`: `usb`,
`nvme`, `sata`), and its partitions with size, label and filesystem.

### 4.2 The firmware

`/sys/firmware/efi` present: UEFI. Absent: BIOS. Read through
`CheckContext::fileExists`.

### 4.3 The layouts

Sizes follow Agama's Tumbleweed product file: system Btrfs with
snapshots, `base_min` 5 GiB, `base_max` 15 GiB, `snapshots_increment`
250 %; swap 1 to 2 GiB; home XFS, at least 5 GiB
(Source: https://raw.githubusercontent.com/agama-project/agama/master/products.d/tumbleweed.yaml,
read 2026-10-15). The start-up partition is larger than Agama's
older default, because kernels now live on it; 1 GiB was reported too
small with the new boot loader
(Source: https://forums.opensuse.org/t/boot-efi-too-small-on-fresh-install/193821).

| Partition | Use | Filesystem | GPT type | Name and label | Size |
|---|---|---|---|---|---|
| Start-up (UEFI) | `efi` | FAT32 | `uefi` | `EFI` | 4 GiB |
| BIOS boot (BIOS) | `bios-boot` | none | `21686148-6449-6E6F-744E-656564454649` | `bios` | 8 MiB |
| Swap | `swap` | swap | `swap` | `swap` | 2 GiB, or memory's size with room to hibernate |
| System | `system` | Btrfs | `linux` | `openSUSE` | the rest, or 52.5 GiB with a separate home |
| Home | `home` | XFS | `home` | `home` | the rest |

The suggestions, each offered only where it fits:

- **Whole drive:** start-up (or BIOS boot), swap, system. Needs at least
  24 GiB (4 + 2 + 17.5, the system's 5 GiB × 350 %, rounded up).
- **Whole drive, separate home:** start-up (or BIOS boot), swap, system
  52.5 GiB (15 GiB × 350 %), home. Needs at least 64 GiB.
- **Beside Windows:** the same partitions as whole drive, in the largest
  free space on a GPT drive with Windows on it, never touching the
  existing partitions. Its start-up partition is a new one; Windows'
  own is left alone. Needs the same 24 GiB of free space.

**Room to hibernate** is a switch on the layout page, for every
layout. On, swap is the size of memory (`MemTotal` in `/proc/meminfo`,
rounded up to a whole GiB) instead of 2 GiB, and each minimum above
grows by the difference. It starts on when that swap is at most 5 % of
the space the layout uses, and off otherwise; when off for that reason,
the switch says how many GB it would take and what share of the drive.
The page also says that memory is this computer's, so a drive prepared
for another computer should be sized for that one.

**Custom:** a list of partitions, each with a use from `efi`,
`bios-boot`, `swap`, `system`, `home` and `data` (XFS, label `data`),
and a size in GiB or "the rest". It is valid when it has exactly one
`system`, an `efi` on UEFI or a `bios-boot` on BIOS, at most one of each
other use but `data`, at most one "the rest", each partition at least
its minimum (the table above; `data` 512 MiB, Agama's unmounted XFS
minimum), and a total that fits. Labels are the fixed ones above;
the user types no text that reaches a command.

Windows is on a drive when any partition's `FSTYPE` is `ntfs` or
`BitLocker`. The checklist page comes first then, and its "Continue"
needs no switch: it advises, it does not gate.

### 4.4 The layout file

The wizard writes the confirmed layout to
`$XDG_STATE_HOME/groundwork/layout.json` and starts:

```
groundwork --worker --prepare-drive LAYOUT_FILE [--lang LANG]
```

```json
{
  "format": 1,
  "drive": { "path": "/dev/sdb", "size": 500107862016,
             "model": "…", "serial": "…", "ptuuid": "…" },
  "firmware": "uefi",
  "kind": "whole",
  "partitions": [
    { "use": "efi", "size": 4294967296 },
    { "use": "swap", "size": 2147483648 },
    { "use": "system", "size": null }
  ]
}
```

`kind` is `whole` or `beside`. `size` is bytes, or `null` for "the
rest". The Worker trusts none of it (§ 5, INV-3, INV-4).

### 4.5 What the Worker does

It runs the job as one item, id `prepare-drive`, with the existing
markers (`docs/reference/worker-interface.md`, Markers):

1. Validate the file by § 4.3's rules, for the drive as it reads it now.
2. Re-read the drive (§ 4.1). Refuse unless `path`, `size`, `model`,
   `serial` and `ptuuid` all equal the file's, and the drive still
   cannot be refused by § 4.1's in-use rule.
3. Write the table with one root step, the script on standard input:
   - `whole`: `sfdisk --wipe always --wipe-partitions always <drive>`,
     script starting `label: gpt`.
   - `beside`: `sfdisk --append <drive>`, each new partition given its
     `start=` inside the free space step 1 found.
4. `udevadm settle`, then re-read the drive to learn the new
   partitions' paths, by order of start.
5. One root step per new partition: `mkfs.fat -F 32 -n EFI`,
   `mkfs.btrfs -f -L openSUSE`, `mkfs.xfs -f -L home` (or `data`),
   `mkswap -L swap`. BIOS boot gets none.
6. Re-read once more and compare with the layout (INV-6).

Every command is bare, as items' are (`docs/design.md`, What every part
does the same way, Tests). `sudo`'s `secure_path` finds `/usr/sbin`,
where these live (`rpm -qf`, 2026-10-15: util-linux, dosfstools,
btrfsprogs, xfsprogs).

### 4.6 Afterwards

The run page reads the drive back and lists, per new partition, what to
set in the installer: mount point (`/boot/efi`, `/`, `/home`, swap, or
none for `data`), and whether to format it. Format the start-up,
system and swap partitions; do not format home or data. The same page
says that the installer must be told to use these partitions rather
than its own proposal.

## 5. Invariants

- **INV-1** — A drive with any mounted device or active swap under it,
  at any depth, cannot be picked.
  *Test:* `tests/tst_drives.cpp`, a fake `lsblk` with a drive whose
  `crypt` grandchild is mounted at `/`; the rule isolated is the depth
  of the walk.
  *Breaks when:* the walk stops at partitions, so an encrypted system
  drive is offered.

- **INV-2** — The Worker writes nothing before a confirmed layout
  exists: the wizard's Apply on the confirm page is unavailable until
  the "I understand" switch is on, and the Worker refuses to start
  `prepare-drive` without `--prepare-drive`.
  *Test:* `tests/tst_wizard.cpp`, the confirm page with the switch off
  and on.
  *Breaks when:* Apply is reachable with the switch off.

- **INV-3** — The Worker writes only to the drive the user saw: it
  refuses when any of `path`, `size`, `model`, `serial`, `ptuuid` read
  now differs from the layout file.
  *Test:* `tests/tst_worker.cpp`, a layout file whose `serial` differs
  from the fake `lsblk`; the run ends `fail` with no `sfdisk` call
  recorded by the fake.
  *Breaks when:* a different stick is plugged in at the same path
  between picking and writing.

- **INV-4** — The Worker validates the layout itself (§ 4.3's rules),
  and refuses an invalid one before any root step.
  *Test:* `tests/tst_worker.cpp`, a layout with two `system`
  partitions; no `sudo` call recorded.
  *Breaks when:* a hand-written or corrupted layout file reaches
  `sfdisk`.

- **INV-5** — A `beside` layout never changes an existing partition:
  its `sfdisk` script uses `--append`, and every new partition starts
  and ends inside free space.
  *Test:* `tests/tst_drives.cpp`, a fake GPT drive with Windows
  partitions and a gap; every new partition lies inside the gap.
  *Breaks when:* a new partition overlaps an existing one, or the
  script lacks `--append`.

- **INV-6** — A run ends `ok` only when the drive read back has the
  layout's partitions, in order, each within one alignment unit (1 MiB)
  of its size, with the filesystem and label of § 4.3.
  *Test:* `tests/tst_worker.cpp`, a fake `lsblk` that reports one
  partition missing after writing; the run ends `fail`.
  *Breaks when:* a failed `mkfs` is reported as success.

- **INV-7** — Every suggestion offered fits: no suggestion is shown for
  a drive or free space smaller than its minimum (24 GiB, or 64 GiB with
  a separate home, each grown by the extra swap when room to hibernate
  is on).
  *Test:* `tests/tst_drives.cpp`, drives of 23 GiB, 24 GiB, 63 GiB and
  64 GiB, with the switch off; and 24 GiB with it on and 8 GiB of
  memory, where whole drive is not offered.
  *Breaks when:* the 64 GiB rule is checked against the whole drive in
  the `beside` case, or the minimum ignores the hibernation swap.

- **INV-9** — Room to hibernate starts on exactly when memory-sized swap
  is at most 5 % of the space the layout uses.
  *Test:* `tests/tst_drives.cpp`, 32 GiB of memory on a 4 TB drive (on)
  and on a 256 GB drive (off, with the share stated).
  *Breaks when:* the share is taken of the whole drive in the `beside`
  case, where the layout uses only the free space.

- **INV-8** — No text the user types reaches a command; every command
  is a fixed argument list, and the `sfdisk` script is built from
  § 4.3's fixed names and computed numbers only (trust boundary: the
  layout file and the wizard's inputs).
  *Test:* `tests/tst_drives.cpp`, the generated script for each
  suggestion compared with an expected script.
  *Breaks when:* a label or name is taken from input.

## 6. Failure modes

- **A tool is missing** (a live image without `xfsprogs`, say): the
  step fails with the tool named; the table may already be written. The
  run page says which tool, and that the drive is partitioned but not
  formatted.
- **`sfdisk` fails** after `--wipe`: the drive may hold no table. The
  run ends `fail` and says the drive must be prepared again.
- **The kernel cannot re-read the table** (a partition still in use):
  `sfdisk` reports it. INV-1 makes this unlikely; the run ends `fail`.
- **The user unplugs the drive mid-run:** the next step fails; the run
  ends `fail`.
- **Stop requested:** stopping is between items, and this is one item,
  so a started write finishes (`docs/design.md`, Stopping).
- **An MBR drive with Windows:** `beside` is not offered; the page
  says to use the installer.

## 7. Tests

- `tests/tst_drives.cpp` (new): INV-1, INV-5, INV-7, INV-8, INV-9,
  with fake `lsblk` JSON and `/proc/meminfo` fixtures.
- `tests/tst_worker.cpp`: INV-3, INV-4, INV-6, driving the built
  program with fake `sudo`, `lsblk`, `sfdisk`, `udevadm` and `mkfs.*`
  first on `PATH`, as it does today.
- `tests/tst_wizard.cpp`: INV-2.
- Manual, in a VM before release: a blank second virtual disk, whole
  drive with separate home, then the installer on it; and a disk
  holding an NTFS partition and free space, `beside`, then the
  installer. Each test is seen to fail before its code exists.

## 8. Alternatives considered (and rejected)

- **`parted` or `sgdisk`** — `sfdisk` is in util-linux, which every
  openSUSE has; it takes a script on standard input, which `Step::input`
  already carries.
- **Shrinking Windows from Groundwork** (`ntfsresize`) — rejected by
  the user's ruling: Windows' own tool knows its files, and Fast
  Startup leaves NTFS in a state Linux tools must not touch.
- **Reusing Windows' start-up partition** — it is often 100 MiB, too
  small for kernels on it
  (Source: https://forums.opensuse.org/t/install-dual-boot-with-existing-small-efi-partition/190517);
  the user chose a new one.
- **Swap always the size of memory** — wastes a large share of a small
  drive; the user asked for a warning instead.
- **Typing the drive's name to confirm** — rejected by the user in
  favour of a switch.

## 9. Out of scope

- Shrinking or moving existing partitions — deferred; not yet queued.
- Free space beside an existing Linux install — deferred; not yet
  queued.
- MBR drives beside Windows — deferred; not yet queued.
- Encryption of the new partitions — deferred; not yet queued.
- Live-system detection and the three-job page — tracked by GRND-0077.

## 10. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/tst_drives.cpp` |
| INV-2 | `tests/tst_wizard.cpp` |
| INV-3 | `tests/tst_worker.cpp` |
| INV-4 | `tests/tst_worker.cpp` |
| INV-5 | `tests/tst_drives.cpp` |
| INV-6 | `tests/tst_worker.cpp` |
| INV-7 | `tests/tst_drives.cpp` |
| INV-8 | `tests/tst_drives.cpp` |
| INV-9 | `tests/tst_drives.cpp` |
| § 4.6's installer guidance is right for Agama and YaST | **nothing** — only the manual VM run in § 7 shows it |

## 11. Cross-doc impact

- `docs/reference/worker-interface.md`: `--prepare-drive`.
- README: the prepare job, once shipped.
- CHANGELOG: an Added entry.

## 12. Cold-eyes loop log

Rows live in `../reviews/GRND-0076-prepare-a-drive-loop-log.md`.
