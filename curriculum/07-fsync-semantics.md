# 07: fsync semantics (and fsyncgate)

**Time:** ~1.5 h. **Runs on:** both (compare them). **Feeds:** M1, M7.

## Concept

**What the calls promise:**

| Call | Promise |
|---|---|
| `fsync(fd)` (Linux) | File data + all metadata durable; issues a device cache FLUSH. |
| `fdatasync(fd)` (Linux) | Data + only metadata *needed to read it* (size yes, mtime no). Cheaper when the size doesn't change. |
| `fsync(fd)` (macOS) | Data sent to the drive. **The drive may still hold it in volatile cache.** |
| `fcntl(fd, F_FULLFSYNC)` (macOS) | Data + drive cache flush. The real durability call. Slow. |
| `fcntl(fd, F_BARRIERFSYNC)` (macOS) | An ordering barrier: I/O before it persists before I/O after it. Not durability. |
| `sync_file_range` (Linux) | Starts or waits for writeback of a range. **No durability promise**: no metadata, no flush. |
| `O_DSYNC` / `O_SYNC` | Each `write` behaves as if followed by `fdatasync` / `fsync`. |

All of them assume the device honors FLUSH. Enterprise SSDs with
power-loss protection make FLUSH nearly free. Consumer drives vary, and
some virtual disks lie.

**Directory fsync.** `fsync` on a directory fd makes its entries durable
(topic 02). On Linux that's `open(dir, O_RDONLY|O_DIRECTORY)` + `fsync`.

**fsyncgate (2018).** PostgreSQL developers discovered that on Linux, when
background writeback of a dirty page fails (e.g. a transient EIO), the
kernel may mark the page **clean** anyway and record the error, and it
reports the error to only one `fsync` caller. A later `fsync` returns
**success**, and the data is gone. So "retry fsync until it works" is
wrong: success after failure proves nothing. Since Linux 4.13 (`errseq_t`),
reporting is more reliable (every fd open at the time of the error sees it
once), but the pages are still dropped. The accepted response is: treat a
failed fsync as **fatal for that file's un-synced data**, stop accepting
writes, and recover from something known to be durable. PostgreSQL now
panics. RocksDB sets a background error and stops writes.

## Exercise: lab 07 (+ bench_fsync)

`curriculum/labs/lab07_fsync_latency.cc`. Implement `DoSync` (per-OS
`#ifdef`s), then run it on the Mac *and* in the VM:

```bash
./build/release/curriculum/labs/lab07_fsync_latency
./build/release/bench/bench_fsync --benchmark_min_time=2s     # after M1: your SyncFd, across sizes
```

Record the table in `curriculum/NOTES.md`. On macOS, how many times slower
is `F_FULLFSYNC` than `fsync`? In the VM, is `fdatasync` faster than `fsync`
for appends, and for overwrites? Is the VM's `fsync` latency plausible for a
real FLUSH to your SSD?

## Checkpoint question

Your WAL's `Sync()` returns EIO. A colleague proposes: "retry up to 3
times, and if it succeeds, carry on." Explain why a successful retry proves
nothing on Linux. What does strata do instead (the M7 contract in
`include/strata/db.h`)? Then the hard part: after the user closes and
reopens the DB, recovery reads the WAL back. Where do those bytes come
from, and is it possible that recovery sees a record that is *not* on
disk? What would that mean for the prefix property, and what in strata's
design limits the damage?

## Engine tie-in

- **M1:** `SyncFd` implements the table above. `kDefaultSyncMethod` is
  `kFullFsync` on macOS for a reason. The rule: retry on EINTR, **never
  after EIO**.
- **M2:** `WriteOptions::sync` = the WAL record is fsynced before `Put`
  returns. `sync=false` gives OS-crash-unsafe but process-crash-safe
  durability. Know which one you're claiming.
- **M7:** the sticky background error (`tests/m7/db_error_test.cc`):
  after a failed sync, flush, or compaction, every write fails until reopen.

## Going deeper

- LWN, *PostgreSQL's fsync() surprise* (2018), and the pgsql-hackers "fsyncgate" thread.
- Rebello et al., *Can Applications Recover from fsync Failures?*, USENIX ATC 2020.
- `man fcntl` (macOS): `F_FULLFSYNC`, `F_BARRIERFSYNC`.
