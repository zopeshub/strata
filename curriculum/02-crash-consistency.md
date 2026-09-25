# 02: Crash consistency

**Time:** ~2 h. **Runs on:** Linux VM (dm-flakey needs root + device mapper).
**Feeds:** M2, M5, M6.

## Concept

**The problem.** `write(2)` copies into the page cache and returns.
Writeback happens later, in whatever order the kernel and the device
choose. A power loss can therefore leave *any subset* of recent
un-`fsync`ed writes on disk, including subsets that never existed in
memory at any instant: the second write persisted and the first didn't.

**What file systems promise.** Journaling (ext4, XFS) and copy-on-write
(APFS, btrfs, ZFS) make the file system's *own metadata* consistent after
a crash, so you won't need `fsck`. They do **not** make *your
application's* data consistent. ext4's default `data=ordered` mode writes
a file's data blocks before committing the metadata that points at them,
which prevents exposing stale blocks, but it says nothing about the order of
two of your writes, or whether a rename is persisted before or after the
data it depends on.

**Persistence properties vary.** Pillai et al. (OSDI '14) tested real
file systems and found they differ on whether appends are atomic, whether
operations on different files persist in order, and whether a rename can
persist before the renamed file's data. Applications written against one
file system's accidental behavior (commonly ext3's `data=ordered`)
silently lose data on others. **Portable code assumes only what POSIX and
fsync promise:**

1. After `fsync(fd)` returns 0, the file's data and the metadata needed to
   read it back (size, block map) are durable.
2. A **new directory entry** (create, rename, unlink) is durable only after
   `fsync` **on the directory**. `fsync` on the file doesn't make its *name*
   durable. (ext4 often does it anyway, which is why this bug hides.)
3. Nothing else, including the order of un-synced operations.
4. Sector writes may tear. Don't assume a 4 KiB write is all-or-nothing.
   Detect torn records with checksums instead.

**The two crash-safe protocols every storage engine uses:**

- **Replace a small file atomically** (CURRENT, config): write temp file →
  `fsync(temp)` → `rename(temp, final)` → `fsync(dir)`.
- **Append to a log**: append records carrying length + checksum →
  `fsync(log)` before acknowledging. Recovery reads until the first record
  that fails its checksum, and treats that point as the end (a torn tail).
  Creating a new log file needs one `fsync(dir)` so the log's *name* survives.

## Exercise: lab 02 (Lima VM)

`curriculum/labs/lab02_durable_create.cc` + `lab02_dmflakey.sh`. The script
builds a loop device, puts a dm-flakey target on it, and runs your program.
It then switches the device to *drop all writes*, unmounts, restores it, and
remounts to see what survived.

1. Implement `CreateAndSyncFile` and `SyncDirectory`.
2. **Predict**, then run all three:
   `curriculum/labs/lab02_dmflakey.sh build/debug file-only ext4`,
   `... file-only ext2`, `... with-dir ext2`.
3. Explain why `file-only` usually survives on ext4 and not on ext2.
   What is ext4's journal doing on your behalf? Why must strata not depend
   on it?

## Checkpoint question

Here is a first draft of how M6 makes a flushed SSTable live:

1. write `000012.sst`, `fsync` it
2. append a VersionEdit ("add file 12 at L0") to the MANIFEST, `fsync` the MANIFEST
3. delete the old WAL

Consider a power loss after each step. For each point, what state does
`DB::Open` find, and is any acknowledged write lost? One step is missing.
Which one, and which failure does it prevent? Does the answer change if
step 1 wrote `000012.dbtmp` and renamed it?

## Engine tie-in

- **M2:** WAL records carry CRC32C + length. `log::Reader` distinguishes a
  torn tail (normal after a crash, silent) from mid-log corruption
  (reported). `DB::Open` syncs the directory after creating a new WAL.
- **M5/M6:** SSTable creation and `SetCurrentFile` are both instances of
  the protocols above. `tests/m6/db_crash_test.cc` uses `MemFileSystem`'s
  `SimulatePowerLoss`, a model of *exactly* rules 1–3: un-synced data and
  un-synced directory entries may vanish.
- The **prefix property** your crash tests check (every acknowledged
  write survives, and the recovered state is some prefix of the write history)
  is this topic's payoff.

## Going deeper

- Pillai et al., *All File Systems Are Not Created Equal: On the Complexity
  of Crafting Crash-Consistent Applications*, OSDI 2014.
- OSTEP ch. 42 (*Crash Consistency: FSCK and Journaling*).
- Linux `Documentation/admin-guide/device-mapper/dm-flakey.rst`.
