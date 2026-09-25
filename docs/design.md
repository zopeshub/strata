# strata design

The reference for every on-disk format and every invariant. Headers in `src/`
point here by section number (e.g. "design.md §4"). When your implementation
deliberately differs, write an ADR in `docs/adr/` and update this file.

Shape: LevelDB's architecture (it is the clearest LSM design there is),
written in C++23, with a pluggable file system for crash testing and a
coroutine I/O layer on top.

---

## 1. Architecture

```
                 Put/Delete/Write                         Get / NewIterator / Scan
                        |                                          |
                        v                                          v
   +------------ DBImpl (mutex_, writer queue, sequence numbers) -----------+
   |   1. append batch to WAL  -------> 000012.log  (fsync if sync=true)    |
   |   2. insert into memtable (mem_)                                        |
   |                                                                          |
   |   mem_ full? -> becomes imm_, new WAL + new mem_, schedule flush        |
   +-------------------------------------------------------------------------+
        | background thread (one)
        v
   flush: imm_ -> TableBuilder -> 000015.sst (L0)      compaction: Ln + Ln+1 -> Ln+1
        |                                                   |
        +---------> VersionEdit --> MANIFEST-000003 (fsync) <+
                                   CURRENT -> "MANIFEST-000003\n"

   read path:  mem_  ->  imm_  ->  L0 files (newest first)  ->  L1 ... L6 (one file per level)
               each SSTable:  bloom filter -> index block -> one data block
```

Module map (dependency order = milestone order):

| Layer | Files | Milestone |
|---|---|---|
| encoding, CRC, RAII fd, POSIX FS | `util/coding`, `util/crc32c`, `fs/*` | M1 |
| internal keys, write batch, WAL, simple memtable, DB open/recover | `db/dbformat`, `db/write_batch`, `db/log_*`, `db/memtable`, `db/filename`, `db/db_impl` | M2 |
| arena, lock-free-read skiplist | `util/arena`, `db/skiplist.h` | M3 |
| SSTable blocks, bloom, builder, reader | `table/*` | M4 |
| merging iterator, table cache, DB iterator, flush, snapshots, ranges | `table/merging_iterator`, `db/table_cache`, `db/db_iter`, `include/strata/range.h` | M5 |
| MANIFEST, versions, leveled compaction | `db/version_edit`, `db/version_set`, `db/compaction` | M6 |
| group commit, background errors, write stalls | `db/db_impl` (rework) | M7 |
| coroutines, async reads | `io/*` | M8 |

## 2. Keys, sequence numbers, write batches

**Internal key** = `user_key | fixed64((sequence << 8) | type)`, `type` ∈
{`kDeletion`=0, `kValue`=1}. Sequence numbers are 56 bits and strictly
increase across the life of the DB (they must survive restarts).

**Ordering** (`InternalKeyComparator`): user key ascending, then sequence
descending, then type descending. Consequence: for any user key, its
versions are contiguous and newest-first, so `Seek(LookupKey(k, snap))` lands
exactly on the newest version visible at `snap`.

**LookupKey(user_key, snapshot)** = the internal key `user_key | (snapshot, kValueTypeForSeek)`.

**WriteBatch wire format** (also exactly the WAL record payload):

```
rep       := sequence:fixed64 | count:fixed32 | op*
op        := 0x01 varstring(key) varstring(value)    Put
           | 0x00 varstring(key)                     Delete
varstring := varint32(length) bytes
```

Header is 12 bytes. Op *i* gets sequence `sequence + i`. `FromContents`
rejects: fewer than 12 bytes, unknown op tag, truncated varstring, or a count
that doesn't match the number of ops actually present.

All integers: fixed-width little-endian; varints LEB128 (7 bits per byte,
low group first, continuation bit 0x80; max 5 bytes for u32, 10 for u64;
the 10th byte of a u64 must be ≤ 0x01).

## 3. Write-ahead log

The log is a sequence of **32 KiB blocks**. Each block holds physical records:

```
+---------+-----------+----------+--------------------+
| crc (4) | len (2)   | type (1) | payload (len)      |
+---------+-----------+----------+--------------------+
crc  = crc32c::Mask(crc32c(type_byte || payload)), fixed32 LE
len  = fixed16 LE, payload length
type = FULL=1 | FIRST=2 | MIDDLE=3 | LAST=4   (0 = zero padding, never written as a record)
```

- A logical record that fits in the rest of the block is `FULL`. Otherwise it
  is split into `FIRST`, `MIDDLE`*, `LAST`.
- If fewer than 7 bytes (a header) remain in a block, they are zero-filled and
  writing continues at the next block. A zero-length `FIRST` is legal.
- **Masking** the CRC (`rotate right 15 + 0xa282ead8`) exists because a CRC of
  data that itself contains CRCs is weak; know why.
- The same format is reused for the MANIFEST.

**Reader semantics** (full spec in `src/db/log_reader.h`; tests in `tests/m2/log_test.cc`):

- Bad CRC, bad length, or unknown type → report corruption via the
  reporter, **drop the rest of the block**, resynchronise at the next block.
- A `MIDDLE`/`LAST` without a preceding `FIRST` → report, drop that fragment.
- A `FIRST`/`FULL` while inside a fragmented record → report, drop the partial record, start over.
- A truncated record at the **end of the file** is *not* corruption. It is
  what a crash mid-write looks like. Return end-of-log silently.
- Zero-filled space (type 0, length 0) is padding, skipped silently.

**Recovery policy (DB level):** replay every live log in file-number order.
With `paranoid_checks=false`, reported corruption is logged and skipped. With
`paranoid_checks=true`, any reported corruption fails `Open` with `Corruption`.
A torn tail is never an error in either mode.

**Durability contract:** `Put` with `sync=true` returns only after the WAL
record (and everything before it) is on stable storage. `sync=false` writes
survive a process crash (they are in the page cache) but not power loss. A
brand-new WAL file also needs its **directory entry** synced before the first
`sync=true` write is acknowledged (`SyncDir`), or power loss can make the file
disappear.

## 4. SSTable format

```
[data block 0][trailer] ... [data block N-1][trailer]
[filter block][trailer]       bloom filter over all USER keys of the table
[properties block][trailer]   TableProperties
[metaindex block][trailer]    "filter.bloom" -> handle, "strata.properties" -> handle
[index block][trailer]        separator key -> data block handle
[footer]                      48 bytes

trailer := compression_type:u8 (0 = none) | masked_crc32c(block_contents || type_byte):fixed32
handle  := varint64(offset) varint64(size)          size excludes the trailer
footer  := metaindex_handle index_handle zero-padding-to-40-bytes magic:fixed64
magic   := 0x3130617461727473 ("strata01" little-endian)
```

**Block** (data, index, metaindex, properties all use it):

```
entry*   := shared:varint32 | non_shared:varint32 | value_len:varint32 | key_delta | value
restarts := restart_offset:fixed32 * num_restarts | num_restarts:fixed32
```

Every `block_restart_interval` entries the key is stored in full (`shared=0`)
and its offset recorded as a restart point. `Seek` binary-searches the
restart array, then scans linearly. An empty block has exactly one restart
(offset 0), 8 bytes total. Readers treat blocks as **untrusted**: any length
or offset that runs off the end yields an iterator with `Corruption` status,
never UB.

**Index block:** one entry per data block. Key is a *separator* `s` with
`last_key(block i) ≤ s < first_key(block i+1)` (the builder may shorten it;
`FindShortestSeparator`). The value is the block's handle. The index entry
for block *i* is written lazily when the first key of block *i+1* arrives (or
at `Finish`).

**Filter block:** `bits[ceil(n·bits_per_key/8), min 8 bytes] | k:u8`, with
k = round(bits_per_key · ln 2), clamped to [1, 30]. Probes via double hashing:
`h = Hash(key, 0xbc9f1d34); delta = rotr(h, 15); probe i = h + i·delta`.
A reader must return "may match" for malformed filters and for `k > 30`
(reserved for future encodings). Filters are built on user keys
(`ExtractUserKey`), so point lookups for a key with many versions check one
filter entry.

**Properties block:** a block whose keys are the property names (sorted) and
whose values are varint64:
`strata.filter_block_size`, `strata.index_block_size`, `strata.num_data_blocks`,
`strata.num_entries`, `strata.raw_key_bytes`, `strata.raw_value_bytes`.
The index block's size is known before it is written: finish it first, then
write filter, properties, metaindex, index, in file order.

**Durability:** the builder never syncs. Whoever creates a table (flush,
compaction) must `Sync()` it **and** `SyncDir()` before referencing it from the
MANIFEST, because a MANIFEST pointing at a file that power loss erased is a
corrupt database.

## 5. MANIFEST, VersionEdit, CURRENT

The MANIFEST is a log (§3 format) of `VersionEdit` records. Replaying all of
them from the start, applying each to an empty Version, reproduces the
current set of live files and the counters. `CURRENT` holds the name of the
live MANIFEST followed by `\n`.

**VersionEdit encoding:** a sequence of `(varint32 tag, payload)`. Absent
optional fields are not encoded. Fields appear in the order below. Repeated
fields appear in their container's iteration order (`deleted_files_` is a
`std::set`, so it's sorted). The encoding is deterministic.

| tag | field | payload |
|---|---|---|
| 1 | comparator name | varstring |
| 2 | log number | varint64 |
| 3 | next file number | varint64 |
| 4 | last sequence | varint64 |
| 6 | deleted file | varint32 level, varint64 file number |
| 7 | new file | varint32 level, varint64 number, varint64 size, varstring smallest, varstring largest |

(5 is reserved; LevelDB used it for compaction pointers.) Decoding fails
with `Corruption` on an unknown tag, a truncated field, or a level
≥ `kNumLevels`. Refusing unknown tags, rather than skipping them, is a
deliberate choice. Be ready to argue it.

**Commit protocol for a version change** (flush, compaction):

1. Write new SSTables, `Sync()` each, `SyncDir()`.
2. Append the VersionEdit to the MANIFEST, `Sync()` the MANIFEST. ← **commit point**
3. Install the new Version in memory.
4. Only now delete files the new Version no longer references (and that no
   older, still-referenced Version needs).

Opening a DB, or when the MANIFEST grows too large: write a fresh
`MANIFEST-n` containing one edit that snapshots the whole Version, `Sync()`
it, `SyncDir()`, then `SetCurrentFile(n)`. That function is the rename dance:
write `n.dbtmp`, `Sync()`, `rename → CURRENT`, `SyncDir()`. Only after that
may the old manifest be deleted.

**Recovery:** read `CURRENT` (must be `MANIFEST-<digits>\n`, else `Corruption`),
replay that MANIFEST (a torn final record is fine, per §3), check every
referenced SSTable exists (else `Corruption`), replay WALs with number ≥ the
recovered log number, then delete every file that is not live (orphans from
a crash mid-flush/compaction).

**The log number:** a VersionEdit from a flush records `log_number = the
WAL that is still needed` (the one after the flushed memtable's WAL). WALs
below it are fully contained in SSTables and may be deleted.

## 6. Compaction

**Picking:** L0 by file count (score = files / `l0_compaction_trigger`);
L*n* ≥ 1 by bytes (score = bytes / limit(n), limit(1) =
`max_bytes_for_level_base`, limit(n+1) = limit(n) × multiplier). Compact the
level with the highest score if ≥ 1. For L*n* ≥ 1, pick files round-robin by
key (remember where the last compaction of that level ended) so all keys get
compacted eventually.

**Inputs:** the picked file(s) at L*n* (for L0: every L0 file overlapping
them, because L0 files overlap each other), plus every L*n+1* file overlapping
their combined key range. Then expand the L*n* inputs if that can be done
without pulling in more L*n+1* files (optional optimization).

**Invariant at L1+ (tested):** files are sorted and **strictly disjoint in
user keys**: `largest_user_key(file i) < smallest_user_key(file i+1)`. So an
output file may not be cut between two versions of the same user key. If it
were, `Get` could find the older version in one file and never look in the
file holding the newer one. Enforce it in `ShouldStopBefore` / the output
cutting logic.

**Drop rules**, applied to each entry of the merged input stream (internal
key order), where `smallest_snapshot` = oldest live snapshot sequence (or
`last_sequence` if none):

1. **Shadowed version:** an entry is dropped if a *newer entry for the same
   user key has already been seen with sequence ≤ smallest_snapshot*. No
   snapshot can see past that newer entry.
2. **Tombstone:** a deletion with sequence ≤ smallest_snapshot may itself be
   dropped **only if** no level deeper than the output level can contain the
   user key (`IsBaseLevelForKey`). Otherwise dropping it resurrects the older
   value below. This is the classic LSM bug.
3. Everything else is kept.

**Output cutting:** close the current output file when it reaches
`target_file_size`, or when its key range would overlap more than
~10 × target_file_size bytes of L*n+2* (grandparents). The next compaction of
L*n+1* would otherwise have to rewrite all of them. Never cut between two
entries with the same user key (see invariant).

**Trivial move:** exactly one input file at L*n*, zero overlapping at L*n+1*,
and little grandparent overlap → emit a VersionEdit that moves it. No I/O.

**Write stalls:** `l0_slowdown_writes_trigger` → each write sleeps ~1 ms
(once). `l0_stop_writes_trigger` → writers wait on a condition variable until
compaction brings L0 below it. Disabled when `disable_auto_compactions`.
Invariant (tested): L0 never exceeds `l0_stop_writes_trigger` files.

**Amplification counters** (`GetProperty`): `user-bytes-written` (key+value
bytes accepted by `Write`), `wal-bytes-written`, `flush-bytes-written`,
`compaction-bytes-read/written`. Write amplification = (wal + flush +
compaction written) / user.

## 7. Concurrency model

- One `std::mutex mutex_` protects: memtable pointers, `imm_`, sequence
  counters, the VersionSet, background-work state, the writer queue,
  snapshot list, the background error.
- **Never hold `mutex_` across file I/O** except where noted and argued
  (e.g. M2 may, deliberately, for simplicity; M7 removes it).
- Readers take `mutex_` only to copy `shared_ptr`s (mem, imm, current
  Version) and the last sequence; then read lock-free.
- M3: the memtable supports one writer concurrent with many readers without a
  lock (skiplist). The writer is serialized by the DB (the group-commit leader
  in M7).
- One background thread does flushes (priority) and compactions.
  `std::condition_variable` + predicate waits; `std::jthread` with
  `std::stop_token` for shutdown if your standard library has it.
- Destruction: stop accepting background work, wait for the running job,
  then destroy. Iterators and snapshots must be released before the DB (the
  DB may assert).

## 8. Error model

- `Status` / `Result<T>` everywhere. No exceptions cross module boundaries.
  (`STRATA_TODO` throws, but only in scaffolding.)
- I/O errors carry the path and errno text.
- **Sticky background error:** a failed WAL sync, flush, or compaction sets
  `bg_error_`. From then on writes, `Flush`, and `CompactRange` return it.
  Reads keep working. Only close + reopen clears it. Never retry a failed
  `fsync`: after EIO the kernel may have dropped the dirty pages and marked
  them clean, so a retry "succeeds" without the data (fsyncgate, 2018).
- Corruption found by reads with `verify_checksums` → `Corruption` from
  `Get` or the iterator's `status()`, never a crash.

## 9. Coroutine I/O (M8)

`Task<T>`: lazy, move-only, owns its frame, symmetric transfer at
`final_suspend`, exceptions captured and rethrown at `co_await`.
`IoContext` has two backends. Blocking (pread inline; completions queued
and resumed from `RunUntilIdle` so the stack doesn't grow) and io_uring (one
SQE per read, `user_data` = `ReadOp*`, completions reaped and resumed from
`RunUntilIdle`). `WhenAll` fans out N tasks and resumes the parent once when
the last completes. Stretch: a coroutine `Table::GetAsync` and a multi-get
that keeps many data-block reads in flight.
