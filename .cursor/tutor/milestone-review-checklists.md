# Milestone review checklists (TUTOR ONLY)

Use these with `/strata-review M<n>` after the gate passes. They list what
**tests cannot catch** (design, C++ quality, latent bugs) and the most
common mistakes. Every item should become either "verified" or a
question/scenario in the review. Never a patch.

Legend: **[bug]** latent correctness issue · **[C++]** rubric item ·
**[ask]** must be defended verbally for a 4+.

---

## M1: coding, crc32c, FileDescriptor, PosixFileSystem, ASSIGN_OR_RETURN

- [C++] `FileDescriptor`:
  - move ctor/assign are `noexcept`, and move-assign is self-safe
    (`fd = std::move(fd)` doesn't close)
  - the moved-from fd is -1
  - `static_assert(sizeof(FileDescriptor) == sizeof(int))` and
    `std::is_nothrow_move_constructible_v`
  - `release()` is `[[nodiscard]]`
- [bug] `close()` is not retried on EINTR (Linux releases the fd anyway).
  The comment states the macOS behavior.
- [bug] `SyncFd`: EINTR is retried; **EIO is never retried**.
  `F_FULLFSYNC` falls back to `fsync` only on "not supported" errors
  (ENOTSUP/EINVAL on some filesystems), *not* on EIO. Errors carry
  context + errno.
- [bug] Every `read`/`pread`/`write` handles **short counts and EINTR** in a
  loop. `pread` loops until n bytes or EOF.
- [bug] `O_CLOEXEC` on every `open`, including LockFile and directory fds.
- [bug] mmap file: `mmap` of a **zero-length file is EINVAL**. Is that
  handled? `MAP_FAILED` is checked (not `nullptr`), and the destructor munmaps.
- [bug] `WritableFile`:
  - `Append` larger than the buffer bypasses it (no quadratic copying)
  - `Sync` = `Flush` + `SyncFd`
  - `Close` flushes and reports errors
  - the destructor is best-effort, with a comment
  - no double-close
- [bug] `GetChildren` skips `.` and `..`. If using `std::filesystem`,
  it uses the `error_code` overloads (no exceptions escape).
- [C++] `DefaultFileSystem`: a function-local static, **intentionally
  leaked**. [ask] Why? (Static destruction order: background threads or
  other statics may use it during exit.)
- [C++] coding: `memcpy`/`std::bit_cast`/`std::byteswap` + `if constexpr
  (std::endian::native == ...)`. **No `reinterpret_cast` loads.** Varint
  decode checks bounds and max length, and `GetVarint*` consumes input only on success.
- [C++] CRC table is `constexpr` (look for a `consteval`/`constexpr` builder
  and `std::array`). [ask] How would they verify it's not computed at runtime?
- [C++] `STRATA_ASSIGN_OR_RETURN`:
  - evaluates `expr` exactly once
  - moves the value
  - uses unique temporaries via `__COUNTER__`
  - works in both `Status` and `Result<U>` functions
  - [ask] Why can't it be used as the only statement of an unbraced
    `if`, and is that documented?
- [C++] `std::span`/`string_view` in, owning types out. `[[nodiscard]]` on
  `Result`-returning functions (the Status class already is).
- Demo in PROGRESS.md: fsync vs F_FULLFSYNC (macOS), fsync vs fdatasync
  (VM), plus crc32c throughput.

## M2: dbformat, WriteBatch, std::map memtable, WAL, filename, DBImpl v1

- [bug] Comparator: user key ascending, then **(seq,type) descending**.
  `LookupKey` uses `kValueTypeForSeek`. [ask] Why the *highest* type?
- [C++] `WriteBatch`:
  - rule of zero (no user-declared special members)
  - `ForEach` is a thin template over a function pointer + `void*`
  - [ask] lifetime of `fn` vs the context pointer, and why not `std::function`
- [bug] `ForEachImpl` detects **count mismatch** (fewer *or more* ops than
  the header claims), truncated varstrings, and unknown tags → Corruption.
- [C++] The memtable's transparent comparator has `is_transparent`, so
  `Get(string_view)` allocates nothing. [ask] How would they prove zero
  allocations? (A counting allocator, or a benchmark with allocation counts.)
- [bug] log writer:
  - `block_offset_` is initialized from the existing file length
  - the trailer (< 7 bytes) is zero-filled
  - **empty records** produce one FULL fragment
  - the CRC covers type + payload, and is masked
- [C++] log reader: the physical-record result is **a type** (enum/variant
  or struct), not magic ints. It follows the spec numbered in `log_reader.h`,
  including "drop the rest of the block".
- [bug] `ParseFileName`:
  - uses `from_chars`
  - rejects `+12.log`, ` 12.log`, `12.log.bak`, `.log`, and overflow
  - accepts >6-digit numbers
- [bug] Recovery replays WALs in **numeric** order, not lexicographic (a
  7-digit number sorts before a 6-digit one as a string). [ask]
- [bug] `Open`:
  - takes the LOCK **before** reading anything
  - creates the new WAL, then **SyncDir**
  - `last_sequence` = max over replayed batches (sequence + count − 1)
- [bug] `Write`: WAL append (+ Sync) **before** memtable apply. On WAL
  failure, the memtable is untouched and the error is returned.
- [C++] `DBImpl` member order: the WAL file is declared before the
  `log::Writer` that references it. The lock is declared first or last?
  [ask] Justify with destruction order.
- [bug] `Destroy`: OK on a missing dir, `Busy` if open, deletes only
  recognized names, removes the dir only if empty.
- Demo: `tools/crash_test.sh` output + a `strata_wal_dump` excerpt. [ask]
  kill -9 vs power loss: which failures does each one exercise?

## M3: Arena, SkipList, concurrent MemTable

- [C++] **Every atomic access** has a memory order and a comment. Probe
  each one:
  - `Next`: acquire, `SetNext`: release
  - pre-publication `NoBarrier_SetNext`: relaxed
  - `max_height_`: relaxed (a stale smaller value just starts lower; a
    larger one finds `head->next == nullptr`)
  - `MemoryUsage`: relaxed

  "seq_cst everywhere" = area 5 score 2 at most.
- [bug] Insert links **bottom-up**, after the node is fully initialized.
  `RandomHeight` uses the writer's RNG only.
- [C++] Node layout: over-allocation + `next_[1]`, or an alternative.
  [ask] Is indexing past `next_[0]` UB? What makes the placement-new'd
  atomics' lifetimes begin? Is `std::launder` needed (answer: not if using
  the pointer returned by placement new)?
- [C++] `static_assert(std::is_trivially_destructible_v<Node>)`, and for entries.
- [bug] Arena:
  - alignment math handles an already-aligned pointer (slop 0)
  - large requests get their own block without abandoning the current one
  - `MemoryUsage` counts block overhead
- [bug] `DBImpl::Get` copies `shared_ptr<MemTable>` under the mutex, then
  reads **without** it. Iterators keep the memtable alive.
- TSan clean on `ctest -L m3` (gate `--tsan`). No suppressions added.
- Demo: before/after `bench_memtable` results in `bench/results/`. [ask]
  Explain one ordering choice without notes.

## M4: format, BlockBuilder, Block, Bloom, TableBuilder, Table

- [bug] `Block` ctor validates `num_restarts` without overflow. Each
  **restart offset read from disk** is checked `< restart_offset_` before
  use (common miss!).
- [bug] Entry decode checks `shared <= last_key.size()` and
  `non_shared + value_len <= remaining`, with no wraparound.
- [C++] The block iterator owns `std::string key_`, and `value()` views
  into the block. [ask] What does each returned view point into, and until when?
- [bug] `ReadBlock`: `offset + size + trailer` is checked against the file
  size **without overflow**. It verifies the CRC over contents + type, and
  rejects unknown types.
- [bug] TableBuilder:
  - the index entry for block *n* is written when block *n+1* starts (or
    at `Finish`)
  - the filter covers **user** keys
  - the status is sticky after the first error
  - `Abandon` vs `Finish` are distinct
- [C++] pimpl / `unique_ptr<Rep>` with the destructor **defined in the .cc**.
  [ask] Why must it be? A passkey or private ctor + factory for `Table::Open`.
- [bug] Bloom: `k` clamped to [1,30], the min 64-bit filter, malformed → true.
- [C++] Stats counters use `memory_order_relaxed` with a justification.
- Demo: `strata_sst_dump --build/--stats/--get` + `bench_bloom`.

## M5: merging iterator, DBIter, flush, snapshots, properties, ScanRange

- [bug] `MergingIterator` propagates the **first child error** and stops. The
  heap vs linear choice is backed by `bench_merge`. [ask]
- [bug] `DBIter`: hides entries with `seq > snapshot`, and a tombstone hides
  all older versions of that user key. After `Seek`, the first key is ≥
  target. `status()` is propagated.
- [bug] Flush:
  - writes `.dbtmp` → Sync → rename → **SyncDir**
  - the old WAL is deleted only after that
  - `imm_` is cleared under the mutex
  - writers wait (predicate CV) when `imm_` is still flushing
- [bug] **L0 order by file number** (newest first), not directory-listing order.
- [bug] `last_sequence` is recovered after WAL deletion (a table scan or a
  max-seq property). [ask] What breaks if it goes backwards? (Newer writes
  would get lower sequence numbers than older SST entries: they'd be invisible.)
- [bug] Snapshot deleter locks the DB mutex. [ask] Can the last reference be
  dropped on a thread already holding it (deadlock)? Iterators pin
  memtables/tables (`shared_ptr` or a `shared_ptr<void>` keepalive).
- [C++] `ScanRange`:
  - `static_assert(std::ranges::input_range<ScanRange>)` and
    `std::input_iterator`
  - move-only
  - `begin()` is callable once (documented)
  - `iter_reference_t` is `pair<string_view,string_view>`
- [bug] Background thread: predicate waits, a shutdown flag, joined in `~DBImpl`,
  and no work after shutdown starts.
- [C++] `GetProperty` formats match `db.h` exactly (tests parse them).
- Demo: two `strata_bench` runs (1M vs 3M) showing read latency vs L0 count.

## M6: VersionEdit, SetCurrentFile, VersionSet, compaction, cleanup

- [bug] `SetCurrentFile`: temp → write → **Sync** → close → rename →
  **SyncDir**. The temp file is removed on failure. The CURRENT content has
  a trailing newline.
- [bug] New MANIFEST on Open: a snapshot record, Sync, **SyncDir** (the new
  MANIFEST's name!), then CURRENT.
- [C++] `LogAndApply` gives the **strong guarantee**: on MANIFEST write
  failure, the in-memory state is unchanged, and the file-number counter is
  consistent. [ask] What happens to the MANIFEST file after a failed append:
  keep appending to it, or roll a new one? (Roll a new one, or make the
  error sticky. Appending after a possibly torn record is wrong.)
- [bug] MANIFEST I/O is done with the DB mutex **released**, with a single
  writer guaranteed (e.g. only the background thread, or a flag).
- [bug] **Pending outputs:** files being written by an in-flight
  compaction or flush are not in any Version. Obsolete-file deletion must
  not remove them (classic race).
- [bug] Obsolete = on-disk − live in **every referenced Version** −
  pending outputs. Log files < `log_number` are obsolete.
- [bug] Compaction:
  - L0 inputs expand to **all overlapping L0 files**
  - L1+ inputs are expanded so no user key straddles the boundary
  - outputs are cut only between different user keys
  - grandparent overlap is bounded
  - drop rules use `smallest_snapshot` and `IsBaseLevelForKey`
- [C++] RAII guard deletes outputs on failure. Dismissed on success, `noexcept`.
- [C++] `current_` swap: [ask] Why is a plain `shared_ptr` assignment racing
  with a reader copying it a data race? (It's under the mutex, or
  `std::atomic<std::shared_ptr>`.)
- [bug] Open deletes orphans (`.dbtmp`, unreferenced `.sst`, old logs), and
  returns Corruption for a missing live file or a malformed CURRENT.
- Demo: `strata_bench` fillrandom/overwrite + WA explanation, and
  `crash_test.sh` with compactions.

## M7: group commit, stalls, sticky errors, shutdown

- [bug] Writer queue:
  - the leader builds the group
  - **the mutex is released during WAL append + Sync** (prove it: point to
    the unlock/lock lines)
  - a non-sync leader must not absorb a sync follower without upgrading
  - the group size is capped
- [bug] **Sequence publication:** `last_sequence_` is advanced *after* the
  memtable insert completes. Otherwise a reader's snapshot could include
  sequence numbers not yet in the memtable (a torn batch visible). [ask]
- [bug] A failed WAL sync means the batch is not applied, the error goes to
  **every member of the group**, and the error is sticky.
- [bug] Stalls: slowdown sleeps with the mutex **released**. The stop wait
  is a predicate wait that also wakes on **shutdown and background error**
  (otherwise writers hang forever).
- [bug] Shutdown under load: the destructor sets the flag, wakes all CVs,
  joins, and leaves no writer blocked.
- [C++] CV protocol: [ask] notify under the lock or after? Why is either
  correct here? Where could a lost wakeup occur?
- [C++] Optional: thread-safety annotations compile with `-Wthread-safety`.
- TSan clean (gate `--tsan`), with no suppressions.
- Demo: the fillsync thread sweep, with `wal syncs` flat while ops/s rise.

## M8: Task, SyncWait, WhenAll, IoContext backends

- [C++] `promise_type`:
  - `initial_suspend` returns `suspend_always` (lazy)
  - `final_suspend` is `noexcept` and returns an awaiter whose
    `await_suspend` returns the continuation or `noop_coroutine()`
  - `unhandled_exception` stores an `exception_ptr`
- [C++] `Task`: move-only, the destructor destroys the frame, move-assign
  destroys the old frame, and self-move is safe. [ask] `operator co_await` on
  `Task&&` only? Why might you forbid awaiting an lvalue task twice?
- [bug] `SyncWait` detects a task that didn't complete (usage error) rather
  than returning garbage.
- [bug] `WhenAll`:
  - no race between synchronous child completion and parent suspension
    (N+1 counter or equivalent)
  - results in input order
  - exceptions: all children complete before rethrowing, and none are
    destroyed while running
- [bug] Blocking backend queues completions (no inline resume), so there's
  no stack growth over 200k reads.
- [bug] io_uring:
  - SQ-full handling (submit then retry `get_sqe`)
  - `cqe_seen` **before** resuming
  - negative `res` → -errno
  - the in-flight count is accurate
  - the destructor drains or waits before `queue_exit`
- [C++] RAII ring wrapper, non-copyable. Error from `queue_init` → `Result`.
- [ask] The lifetime ADR: Task destroyed with a read in flight (topic 06
  checkpoint). Reference parameters of coroutines (dangling after the
  caller's full-expression).
- Demo: `bench_async_read` in the VM, queue-depth scaling vs `lab09_fio` numbers.
