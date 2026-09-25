# Checkpoint answer key (TUTOR ONLY: never show the learner)

Format per topic: **must-hit points** (all needed for PASS), **probes** to
use when a point is missing, **misconceptions** to name as wrong, and a
**going further** item that's shared only after the verdict.

---

## 00 Setup: layers under the VM's fsync

**Must hit**
1. The guest `fsync` writes the guest page cache's dirty pages plus the ext4
   journal commit. The guest block layer then sends a **FLUSH (and/or
   FUA)** to the virtual disk.
2. The hypervisor (Virtualization.framework, used by Lima `vz`) must turn
   that FLUSH into host durability on the disk-image file. On macOS that
   means `F_FULLFSYNC` (or equivalent), **not plain `fsync`**, which doesn't
   flush the drive cache.
3. APFS on the host, then the SSD controller: data must leave the drive's
   volatile write cache (or the drive has power-loss protection).
4. The layer you **can't determine from inside the guest** is how the
   hypervisor maps the guest's FLUSH (its disk "synchronization mode"). The
   guest sees `fsync` return 0 either way. Only latency hints at it: if the
   guest's fsync is much faster than the host's `F_FULLFSYNC`, the flush isn't
   reaching NAND.

**Probes**
- "What does ext4 send to the block device at the end of an fsync, besides data?"
- "If the hypervisor implemented FLUSH as `fsync()` on the image file on macOS, where could the data still be?"
- "How could a timing experiment tell you something the API can't?"

**Misconceptions**
- "The VM disk is a file, so the host page cache makes it durable." The page cache is volatile.
- "virtiofs vs disk doesn't matter for durability." It does. The mount has its own caching.

**Going further:** Apple's `VZDiskImageSynchronizationMode` (full / fsync /
none) is exactly this knob. Compare the lab 07 numbers in the VM with the
host `F_FULLFSYNC` row.

---

## 01 Filesystems: deleting an SSTable that's still open

**Must hit**
1. `unlink` removes the **directory entry** and decrements the inode's link
   count. The inode and its blocks persist while any **open file
   description** (or mmap) references it. The iterator's fd still refers to
   the inode, so reads return the old bytes. Blocks are freed on the last close.
2. Reusing file numbers breaks the name → content mapping over time:
   - The `TableCache` (keyed by file number) could serve a stale open
     handle to the *old* inode for a *new* file with the same name, or the
     reverse.
   - A MANIFEST edit "delete N" / "add N" becomes ambiguous.
   - Obsolete-file deletion could delete a *new, live* file that reused the
     name of a dead one.
3. Monotonic `next_file_number` (persisted in the MANIFEST) guarantees that
   a name refers to at most one content for the life of the DB.

**Probes**
- "What exactly does the iterator hold: a path, or something else?"
- "Obsolete-file deletion runs while a compaction is writing file 12. The old file 12 is dead. What gets deleted?"

**Misconceptions**
- "The kernel copies the file somewhere when you unlink it." It doesn't.
- "Unlinking an open file fails." That's true on Windows, not POSIX.

**Going further:** `pending_outputs_` in LevelDB exists for the same reason.
Files being written by an in-flight compaction aren't in any Version yet,
and must not be treated as orphans.

---

## 02 Crash consistency: making a flushed SSTable live

**Must hit**
1. **Crash during or after step 1, before step 2:** the SST (partial or
   complete) is unreferenced. The WAL still holds the data, so recovery
   replays it and deletes the orphan. No loss.
2. **After step 2:** the MANIFEST references `000012.sst`, but its
   **directory entry was never made durable**, so after power loss the file
   may not exist. Open fails with Corruption ("missing SSTable"). Worse, if
   the edit also advanced `log_number`, the WAL is considered obsolete and
   the data exists nowhere.
3. **The missing step:** `fsync` the **directory** after creating the SST
   (and after the rename, if using `.dbtmp`), *before* the MANIFEST edit
   references it.
4. **After step 3:** fine. Deleting the WAL needs no dir sync. If the unlink
   is lost, the WAL reappears, is recognized as obsolete via `log_number`,
   and is deleted as an orphan. It must **not** be replayed as live.
5. **With `.dbtmp` + rename:** the rename is also a directory operation and
   needs the same dir fsync. In M6, the MANIFEST (not the directory listing)
   defines liveness, so a partial file is just an orphan. The rename matters
   mainly in M5's "every `.sst` is L0" world.

**Probes**
- "Which operation creates the *name* `000012.sst`, and what makes a name durable?"
- "After step 2, what does `Open` look up, and what if it's not there?"
- "What would replaying an obsolete WAL do, and is it harmful?" (It re-applies
  older sequence numbers: wasted work, and it resurrects nothing because
  newer versions have higher sequence numbers. But the design says don't.)

**Misconceptions**
- "fsync on the file makes the file exist." It makes the data durable, not the name.
- "ext4 handled it in my lab, so it's fine." That's accidental, not guaranteed.

**Going further:** the MANIFEST file itself is new on every `Open`. It needs
the same treatment before `CURRENT` points at it. `SetCurrentFile` syncs
the dir for CURRENT, but who syncs the dir entry for the new MANIFEST?

---

## 03 Virtual memory: I/O error under mmap

**Must hit**
1. **pread mode:** the device error completes the block request with an
   error. The page isn't uptodate, so `pread` returns -1/EIO. `Read` returns
   an IOError `Status`, the iterator's `status()` becomes non-OK, and the
   caller handles it. Normal control flow.
2. **mmap mode:** the access page-faults. The kernel attempts the read,
   fails, and delivers **SIGBUS** to the faulting thread. The default action
   kills the process. No `Status` ever exists. The failing instruction may
   be in the middle of a `memcpy` or comparator inside the engine.
3. **Surviving it:** a SIGBUS handler with `sigsetjmp`/`siglongjmp` around
   mapped accesses (per-thread jump buffers). In C++ that's hazardous:
   `longjmp` across frames with non-trivial destructors is UB and is
   async-signal-unsafe territory. Alternatives: copy via a guarded
   primitive, fall back to pread for the file after a fault, or accept a
   crash plus restart. Also note SIGBUS on file truncation.
4. **Default?** A defensible answer is "no, pread by default, mmap opt-in",
   given error handling, the lack of eviction control, and TLB shootdowns.
   Any answer is fine *if justified*.

**Probes**
- "In mmap mode, which line of *your* code is executing when the error surfaces?"
- "Can a signal handler return normally after SIGBUS on a mapped read? What happens then?"

**Misconceptions**
- "`Read` returns an error in mmap mode too." There's no syscall there to return one.
- "mmap avoids the page cache." It *is* the page cache.

**Going further:** Crotty et al. CIDR '22, §3. LevelDB mmaps a bounded
number of files by default, and RocksDB defaults to pread. Ask them why a
limit exists at all (virtual address space, page-table memory, fd count).

---

## 04 Page cache: cost of a Get, and a block cache

**Must hit**
1. Get path: memtable, then imm (in memory), then L0 files newest first.
   For files 1 and 2, the key-range check and/or **bloom filter** (in memory,
   ~1% FPR at 10 bits/key) avoids any data-block read. File 3 costs index
   lookup (in memory) plus **one data-block pread**.
2. With their numbers, roughly: fully cached ≈ one ~1–5 µs pread plus
   parsing ≈ a few µs; cold ≈ one device read (tens to hundreds of µs; more
   in a VM). Without blooms, cold could be up to 3 device reads.
3. **Block cache gains:**
   - control over what stays cached (pin index/filters, per-DB budget)
   - no syscall or copy per hit
   - caching *decoded/uncompressed* blocks
   - with O_DIRECT: no double caching, and compaction can't pollute the cache
4. **Costs:**
   - complexity (sharding, eviction, lock contention)
   - double memory use if not O_DIRECT
   - with O_DIRECT: losing kernel readahead and adding alignment constraints
   - a cold cache after process restart (the page cache survives a restart;
     a user-space cache doesn't)
5. **Measure first:** the working set vs RAM, the syscall share of Get
   latency (perf), and hit rates. strata has no compression, which removes
   one of the biggest wins.

**Probes**
- "Which parts of a Table are in memory after `Open`, and which are read per Get?"
- "What survives a process restart: your cache or the kernel's?"

**Misconceptions**
- "A cached read is free." It's still a syscall plus a copy, around a µs.
- "O_DIRECT is always faster." Not without your own caching and readahead.

**Going further:** RocksDB's `cache_index_and_filter_blocks` and why pinning
L0 filters matters.

---

## 05 I/O paths: fsync of an append on ext4

**Must hit**
1. The device operations for a 4 KiB append + fsync (ext4, `data=ordered`),
   roughly:
   1. the **data block write**
   2. the **journal write** (descriptor plus metadata blocks: inode with
      the new size and mtime, extent/bitmap changes from block allocation)
   3. a **commit block** with **PREFLUSH + FUA** (bpftrace `rwbs` shows
      F/FUA flags), or a separate FLUSH
2. The inode size update and the block allocation (extent tree, bitmap)
   exist **because the file grew**. mtime changes alone would be skipped by
   `fdatasync`.
3. Technique A: **preallocate and overwrite in place.** Write zeros ahead
   of time (not just `fallocate`: unwritten extents still need a metadata
   conversion on first write), then use `fdatasync`, and each sync is
   mostly the data write plus a flush.
   Cost: the upfront zero-writes, and the reader must detect the end of
   valid records inside a preallocated file.
4. Technique B: **recycle old log files** (RocksDB `recycle_log_file_num`),
   the same effect without upfront zeroing.
   Cost: stale records from a previous incarnation must be distinguishable,
   so the record header includes the log number.
   (Also acceptable: `O_DIRECT|O_DSYNC` on a preallocated file gives
   FUA-only writes, with alignment costs. Or: group commit amortizes
   instead of eliminating.)

**Probes**
- "Which of those writes would disappear if the file's size didn't change?"
- "Does `fallocate` make the next write metadata-free? Why not?"

**Misconceptions**
- "One fsync = one write." That's false on every journaling FS.
- "fdatasync never touches metadata." It does when the size changes.

**Going further:** ext4 fast commits (5.10+) shrink step 2.

---

## 06 io_uring: destroying a Task with a read in flight

**Must hit**
1. `~Task` → `handle.destroy()` frees the coroutine frame, including the
   awaiter's `ReadOp` and any buffer inside the frame.
2. The kernel still owns the SQE: it will **write the read's data into
   `buf`**, which is now freed memory (heap corruption), then post a CQE whose
   `user_data` points at the freed `ReadOp`.
3. The reaper dereferences it, reads `waiter`, and **resumes a destroyed
   coroutine**: UB, typically a crash far from the cause.
4. Two safe designs, e.g.:
   - **(a) Structured concurrency:** make it impossible. A Task can only
     be destroyed after completion (assert or terminate in `~Task` if
     suspended on I/O). `WhenAll` and `RunUntilComplete` always await
     children.
   - **(b) Detach op state from the frame:** a heap-allocated op that owns
     its buffer and is freed by the context when the CQE arrives. On destroy,
     mark it cancelled, submit `IORING_OP_ASYNC_CANCEL`, and still wait for
     the CQE.
   - **(c) Block in the destructor** until the CQE arrives. Correct, but
     blocking in a destructor.
5. io_uring provides `ASYNC_CANCEL`, but cancellation **doesn't release the
   buffer early**. You always get a CQE, and until then the kernel may write.
   Regular-file reads already issued to the device often can't be cancelled.

**Probes**
- "After `destroy()`, what memory does the kernel still hold a pointer to?"
- "Cancel succeeded. Can you free the buffer now?"

**Misconceptions**
- "The kernel copies into its own buffer first." Not for these ops: it writes to yours.
- "Cancel is synchronous."

**Going further:** why `io_uring_register_buffers` makes lifetime bugs
*worse* (the pages are pinned, so the kernel happily writes to recycled
memory).

---

## 07 fsync semantics: EIO from the WAL sync

**Must hit**
1. On a writeback error, Linux may mark the pages **clean** and report
   the error once (errseq). A retry finds nothing dirty and returns 0, but
   the data never reached the device. **A successful retry proves nothing.**
2. strata's contract: the failing write **returns the error** and is **not
   applied** to the memtable. The DB sets a **sticky background error**:
   all subsequent writes, Flush, and CompactRange fail, and reads still
   work. The only way out is close + reopen.
3. On reopen, recovery reads the WAL **through the page cache**. The
   un-persisted pages may still be resident (clean but never written), so
   recovery can see a record that is **not on disk** and apply a write the
   user was told failed.
4. Why damage is limited in strata:
   - the sticky error means nothing was appended after the failed record, so
     the uncertain region is always the **WAL tail**
   - strata opens a **new WAL** on every Open, so it never appends after
     the uncertain region

   If the phantom record later vanishes (page evicted, then another crash),
   it looks like a torn tail. The prefix property of *acknowledged* writes
   holds. The remaining anomaly: a reader may have *observed* an
   unacknowledged write that later disappears.
5. Mitigations for that anomaly: flush the recovered memtable to an SST
   and sync at open, read the WAL with O_DIRECT during recovery, or
   PostgreSQL-style: accept it.

**Probes**
- "After the retry returns 0, where are the bytes?"
- "Where does `read()` during recovery get its data from, if the pages are still cached?"
- "Could anything have been appended to that WAL after the failed sync? What guarantees that?"

**Misconceptions**
- "Reopening re-reads from disk, so it's clean." Not necessarily: page cache.
- "errseq_t fixed fsyncgate." It fixed *reporting*, not data loss.

**Going further:** Rebello et al. ATC '20. Ask what ext4 vs XFS vs btrfs do
to the page contents after a failed writeback.

---

## 08 WAL pattern: can k2 survive without k1?

**Must hit**
1. **No, in strata's single-WAL design.** A's `Put` returned before B's
   began, so A's record got a lower sequence number and was appended to the
   **same log file earlier** (possibly still in the user-space buffer).
2. B's sync first **flushes the user-space buffer** (which contains A's
   record), then calls `fsync`. `fsync` makes **all written data of the file**
   durable, not just B's range. So "k2 durable" implies "k1 durable".
3. Recovery reads the log in order and **stops at the first bad record**, so
   it can't skip a hole and keep what follows.
4. **Sharded WALs:** B's fsync covers only B's shard, so after power loss k2
   can survive while k1 doesn't. That violates the prefix property and
   causality. Fixes: B's sync must sync every shard containing lower
   sequence numbers, or recovery replays only up to the largest S such that
   *all* records ≤ S are present across shards. The second fix alone can
   discard B's acknowledged k2, so it needs the first.

**Probes**
- "Where is A's record at the instant B calls Sync: user-space buffer, page cache, or disk?"
- "Does fsync take a byte range?"
- "With two files, what ordering does anything guarantee between them?"

**Misconceptions**
- "sync=false writes can be lost even if a later sync write survives." Not with one log and in-order appends.

**Going further:** that's why WAL sharding designs (e.g. per-core logs in
research systems) need a global epoch or sequence watermark at recovery.

---

## 09 SSD internals: expected write amplification

**Must hit**
1. Level targets: L1 10 MiB, L2 100 MiB, L3 1 GiB, L4 10 GiB, so a 10 GiB
   dataset lives mostly in L4 (4 levels below L0).
2. Per user byte:
   - WAL: 1
   - flush: 1
   - L0 → L1: ~1–2
   - each Li → Li+1 move rewrites up to ~multiplier (10) bytes of the next
     level: ~10 per level for L1→L2, L2→L3, L3→L4

   Upper bound ≈ 1 + 1 + ~2 + 3×10 ≈ **~30–35**.
3. The measured value is usually **lower**, because of:
   - average overlap below the full fanout (the next level isn't full, and
     compaction picks files round-robin)
   - the last level isn't at steady state
   - **trivial moves** (no rewrite), especially for sequential keys
   - overwrites and deletes dropped during compaction
   - **compaction debt at benchmark end** (work not yet done isn't counted)

   It can be higher for small datasets with frequent L0→L1 compactions.
4. **Low device WA:** LSMs write large, sequential, immutable files and
   delete them **whole**. Erase blocks tend to become entirely invalid
   together ("grouping by death time"), so FTL GC copies little live data,
   and TRIM/discard informs the FTL. Contrast: B-tree random in-place 4 KiB
   updates.

**Probes**
- "Where does a byte sit after it leaves L1, and how many times has it been rewritten?"
- "What would happen to measured WA if you ran `compact` at the end of the benchmark?"
- "Why do whole-file deletions help the FTL specifically?"

**Misconceptions**
- "WA = number of levels." It ignores the fanout factor per level.
- "Sequential writes mean WA 1 on the device, always." GC and over-provisioning still matter; they're just friendlier.

**Going further:** RocksDB's dynamic level sizing (`level_compaction_dynamic_level_bytes`)
exists because the last level is rarely at its target size.
