# strata roadmap

Eight milestones. Each one ends with a **commit** and a **demo you can run and
show someone**, and names the **C++ it exercises**. Tutor reviews grade those
C++ features explicitly (`docs/cpp-rubric.md`), not just whether the tests pass.

## How every milestone works

1. **Read** the curriculum topics listed under *Prerequisites* and answer
   their checkpoint questions with the tutor (`/strata-checkpoint 03`).
2. **Implement.** `tools/todo_report.sh M<n>` lists every `STRATA_TODO` site.
   Tests are the executable spec: `ctest --preset debug -L m<n>`. Red tests
   point at the exact TODO (file:line).
3. **Stuck?** Hints: `docs/hints/m<n>.md` has three tiers. Take one tier at a
   time, and try for 20 minutes before taking the next one.
4. **Gate:** `tools/check_milestone.sh M<n>` (plus `--asan` / `--tsan` where
   listed). It must print PASS. It runs the strict preset (`-Werror
   -Wconversion`), and every earlier milestone's tests as regressions.
5. **Demo:** run the demo below and paste its output into `PROGRESS.md`.
6. **ADR:** at least one `docs/adr/NNNN-*.md` per milestone for the biggest
   design decision you made (template in `docs/adr/0000-template.md`).
7. **Commit** with the message below (local commit; push for review only via
   `refs/for/<branch>` if you ever put it on Gerrit).
8. **Review:** `/strata-review M<n>`. The tutor grades your C++ against the
   rubric and the milestone checklist. Fix what it finds, then update `PROGRESS.md`.

Time estimates assume focused evenings. They're guesses; track your actuals in `PROGRESS.md`.

---

## M0: Scaffold (done)

- **State:** everything builds; only the harness tests pass; everything else
  fails with `TODO(Mx) not implemented: ...`.
- **Do:** `cmake --preset debug && cmake --build --preset debug && ctest --preset debug -L harness`.
  Set up the Lima VM (`curriculum/00-setup.md`) and build there too.
- **Demo:** `ctest -L harness` green; `lab05_io_path_probe sync` runs on both
  macOS and Linux.
- **Commit:** `milestone 0: scaffold` (already made).

---

## M1: Foundations: bytes, checksums, files (≈ 6–8 h)

**Implement:** `util/coding` (fixed/varint encode+decode), `util/crc32c`
(table-driven, constexpr table, mask/unmask), `fs/file_descriptor` (RAII fd,
`SyncFd` for all four `SyncMethod`s), `fs/posix_fs` (every file class,
`LockFile` with `flock`, `SyncDir`, `DefaultFileSystem`), and
`STRATA_ASSIGN_OR_RETURN` in `status.h`.

**Tests:** `ctest -L m1` (coding, crc32c, file_descriptor, posix_fs).
Gate: `tools/check_milestone.sh M1 --asan`.

**Demo:** durability costs, measured on your hardware.
```
./build/release/bench/bench_fsync --benchmark_filter='Append'      # macOS
limactl shell strata -- ./build/release/bench/bench_fsync ...        # Linux VM
./build/release/bench/bench_crc32c
```
Put the table in `PROGRESS.md`: fsync vs F_FULLFSYNC on macOS, and fsync vs
fdatasync on Linux. Is CRC ever the bottleneck next to fsync?

**C++ graded:**
- RAII and move-only types (`FileDescriptor`: rule of five done right,
  `noexcept` moves, self-move-assignment safety, `sizeof == sizeof(int)`).
- `std::endian`, `std::bit_cast`, `std::byteswap` vs `memcpy` vs
  `reinterpret_cast`: which of these are UB and why.
- `constexpr` computation of the CRC table.
- `[[nodiscard]]` and `std::expected` propagation.
- A macro that declares a variable (`ASSIGN_OR_RETURN`) and its hygiene.
- `std::span` at API boundaries.

**Rubric focus areas (must reach 4):** 1 ownership, 2 value semantics, 4 error handling, 8 UB hygiene.
**Prerequisites:** 00-setup, 01-filesystems, 07-fsync-semantics.
**Commit:** `milestone 1: coding, crc32c, RAII file descriptors, POSIX file system`
**Stretch:** hardware CRC32C (ARMv8 `__crc32cd`, SSE4.2), selected at compile time.

---

## M2: Durable KV: WAL + memtable + recovery (≈ 12 h)

**Implement:**
- `db/dbformat`: internal keys and comparator.
- `db/write_batch`: wire format; `ForEach` with type erasure.
- `db/memtable` backed by `std::map` with a transparent comparator.
- `db/log_writer` and `db/log_reader`: the full corruption-handling spec.
- `db/filename`: `ParseFileName` with `std::from_chars`.
- `db/db_impl`: `Open` (LOCK, create/exists checks), `Recover` (replay every
  `.log`), `Write`/`Put`/`Delete`/`Get`, `Destroy`. One mutex; holding it
  across I/O is allowed *for now*.

**Tests:** `ctest -L m2`, including power-loss recovery with torn tails
(`db_recovery_test`, MemFileSystem). Gate: `tools/check_milestone.sh M2 --asan`.

**Demo:** *"WAL replay recovers state after kill -9."*
```
tools/crash_test.sh --build build/debug --iterations 50
./build/debug/tools/strata_wal_dump /tmp/strata_crash/000*.log | head -40
```
Then explain the difference between that and `db_recovery_test`, which
simulates **power loss**.

**C++ graded:**
- `std::variant` + `std::visit` + the `Overloaded` idiom (`BatchOp`, `LookupResult`).
- Type erasure for `WriteBatch::ForEach`: a template front end over a
  non-template `ForEachImpl`. Why not `std::function`?
- Forwarding references and `std::invoke`.
- Heterogeneous lookup (`is_transparent`) so `Get(string_view)` doesn't allocate.
- `std::from_chars` for untrusted input.
- Member declaration order = construction order; destruction order in `DBImpl`.
- Rule of zero for `WriteBatch`.

**Rubric focus areas (must reach 4):** 1 ownership, 3 interface design, 4 error handling, 6 generic programming.
**Prerequisites:** 02-crash-consistency, 08-wal-pattern.
**Commit:** `milestone 2: WAL, memtable, crash recovery`
**Stretch:** reuse the last WAL on reopen instead of always starting a new
one. Which crash case does that make harder?

---

## M3: Concurrent memtable: arena + lock-free skiplist (≈ 8–10 h)

**Implement:** `util/arena`, `db/skiplist.h` (acquire/release publication,
relaxed `max_height_`), and `MemTable` on arena + skiplist (same interface;
all M2 memtable tests must still pass). Then `DBImpl::Get` reads the
memtable without holding the DB mutex.

**Tests:** `ctest -L m3` (arena, skiplist incl. concurrent reader/writer,
concurrent memtable). Gate: `tools/check_milestone.sh M3 --tsan --asan`.
**TSan is mandatory here.**

**Demo:** the before/after of a data-structure change.
```
# keep bench/results/m2_memtable.txt from BEFORE you start M3 (run it now!)
./build/release/bench/bench_memtable > bench/results/m3_memtable.txt
```
Show inserts/s and Get scaling for 1→8 threads, before vs after. Explain one
memory-order choice to the tutor without notes.

**C++ graded:**
- **The memory model:** every `std::atomic` access with the weakest correct
  order, each justified in a comment. "seq_cst everywhere" fails review.
- Placement new, alignment (`alignof`, `std::align`), `std::launder`: when
  it's needed and when it isn't.
- Implicit object creation.
- `static_assert(std::is_trivially_destructible_v<...>)` for arena objects.
- Concepts (`SkipListComparator`).
- Non-copyable, non-movable types, and why.

**Rubric focus areas (must reach 4):** 5 concurrency and memory model, 7 performance, 8 UB hygiene.
**Prerequisites:** 03-virtual-memory.
**Commit:** `milestone 3: arena + lock-free-read skiplist memtable`
**Stretch:** a `Prev()` that is O(1) with back-pointers. What does it cost the writer?

---

## M4: SSTables (≈ 10 h)

**Implement:** `table/format` (handles, footer, `ReadBlock` + CRC),
`table/block_builder`, `table/block` (untrusted input!), `table/bloom`,
`table/table_builder` (lazy index entries, filter, properties, metaindex,
footer), and `table/table` (`Open`, two-level iterator, `Get` with bloom, stats).

**Tests:** `ctest -L m4`. Gate: `tools/check_milestone.sh M4 --asan`. ASan
proves the corruption cases don't read out of bounds.

**Demo:** a bloom filter saving I/O.
```
./build/debug/tools/strata_sst_dump --build=100000 /tmp/demo.sst
./build/debug/tools/strata_sst_dump --stats /tmp/demo.sst
./build/debug/tools/strata_sst_dump --get=key00000002 /tmp/demo.sst   # 1 block read
./build/debug/tools/strata_sst_dump --get=key00000001 /tmp/demo.sst   # 0 blocks: bloom said no
./build/release/bench/bench_bloom     # measured FPR vs the formula
```
**Page-cache tie-in:** run `lab04_page_cache` against `/tmp/demo.sst`. Are
its index and filter blocks resident after a lookup? Its data blocks?

**C++ graded:**
- Pimpl and its cost.
- The passkey idiom, or a private constructor with a factory.
- `std::unique_ptr` with incomplete types (where must the destructor be defined?).
- `string_view` lifetimes: every view returned by a block iterator, and what it points into.
- Bit manipulation with `<bit>` (`std::rotr`).
- Defensive parsing without UB: signed/unsigned, overflow in `offset + size`.
- `std::atomic` statistics counters with relaxed ordering, justified.

**Rubric focus areas (must reach 4):** 1 ownership (views!), 3 interface design, 8 UB hygiene.
**Prerequisites:** 04-page-cache, 03-virtual-memory (for mmap reads).
**Commit:** `milestone 4: SSTable builder and reader with bloom filters`
**Stretch:** `use_mmap_reads` in the POSIX FS (zero-copy `Read`). Benchmark it
against pread in `bench_block` terms. Or: a real `FindShortestSeparator`, and
measure the index-size win.

---

## M5: Read path, flush, iterators, snapshots, ranges (≈ 12 h)

**Implement:**
- `table/merging_iterator` and `db/table_cache`.
- `db/db_iter`: user-key view, snapshots, tombstones.
- In `DBImpl`: immutable memtable plus a background flush thread; flushed
  WALs are deleted.
- `Get` across mem → imm → L0 (newest first), and `NewIterator`.
- `GetSnapshot`, with release via a `shared_ptr` deleter.
- `GetProperty`: level file counts, `stats`, `memtable-bytes`, and the byte
  counters.
- The C++20 range in `include/strata/range.h`.

No MANIFEST yet: at open, **every `*.sst` is an L0 file**. Two consequences
to handle (see the hints):
- A crash mid-flush must never leave a partial file named `*.sst`.
- `last_sequence` must be recovered even after the WAL holding it was deleted.

Feeling that pain is the point: it motivates M6.

**Tests:** `ctest -L m5`, including a randomized model check against
`std::map`. Gate: `tools/check_milestone.sh M5 --asan`.

**Demo:** read amplification you can see.
```
./build/release/bench/strata_bench --benchmarks=fillrandom,readrandom,readmissing,readseq,stats \
    --num=1000000 --disable_auto_compactions=1
```
Then run it again with `--num=3000000`. L0 grows with no compaction, and
`readrandom` gets slower. Plot latency against the L0 file count. This is the
problem M6 solves.

**C++ graded:**
- **Ranges:** `ScanRange` as a real `std::ranges::input_range`, with an
  iterator/sentinel pair, `iter_reference_t` = `pair<string_view,string_view>`,
  composing with `std::views`.
- Heap algorithms vs `std::priority_queue`.
- `shared_ptr` custom deleters (snapshot release) and the aliasing constructor.
- `shared_ptr<void>` as a type-erased keepalive.
- `std::thread` + `std::condition_variable` with predicate waits, and a clean shutdown.

**Rubric focus areas (must reach 4):** 1 ownership, 5 concurrency, 6 generic programming (ranges).
**Prerequisites:** 05-io-paths.
**Commit:** `milestone 5: flush, merged reads, DB iterators, snapshots, ranges`
**Stretch:** a user-space LRU block cache (sharded). Measure `readrandom`
with the page cache warm vs cold, with and without it.

---

## M6: MANIFEST, versions, leveled compaction (≈ 16 h, the big one)

**Implement:**
- `db/version_edit` Encode/Decode.
- `db/filename` `SetCurrentFile` (the rename dance).
- `db/version_set`: Versions as immutable `shared_ptr<const Version>`,
  `LogAndApply`, and `Recover`.
- `db/compaction`: picking, inputs, drop rules, output cutting, trivial move.
- Obsolete-file deletion.
- `CompactRange`.
- Open-time cleanup of orphans.

**Tests:** `ctest -L m6`:
- `version_edit` and CURRENT under failed syncs.
- Compaction shape invariants, read through `strata.files`.
- Tombstone resurrection and snapshot pinning.
- A model check with compactions.
- **`db_crash_test`**: power loss at random moments during flushes and
  compactions, torn tails everywhere, crashes during recovery.

Gate: `tools/check_milestone.sh M6 --asan`.

**Demo:** write amplification and a healthy tree.
```
./build/release/bench/strata_bench --benchmarks=fillrandom,overwrite,readrandom,stats --num=5000000
tools/crash_test.sh --build build/release --iterations 100    # now with flushes + compactions under kill -9
```
Explain the printed write amplification from first principles (level
multiplier 10 means about 10× per level, so why is the measured number
lower?). Compare the `readrandom` latency with M5's demo.

**C++ graded:**
- Immutable shared state (`shared_ptr<const Version>`) and how the current
  pointer is swapped (why is an unsynchronized `shared_ptr` assignment a data race?).
- RAII guards for in-progress work (outputs deleted on failure): a
  `scope_exit`-style guard, and its `noexcept`/dismiss design.
- `std::jthread` + `std::stop_token` (if your libc++ has them) or an
  equivalent.
- Exception/Status safety of `LogAndApply`: the in-memory state stays
  unchanged if the MANIFEST write fails.

**Rubric focus areas (must reach 4):** 1 ownership, 4 error handling (strong guarantee), 5 concurrency.
**Prerequisites:** 02-crash-consistency (again: the rename dance),
09-ssd-internals (write amplification).
**Commit:** `milestone 6: MANIFEST, versions, leveled compaction, crash-safe`
**Stretch:** subcompactions (split one big compaction by key range across
threads). Or a universal/tiered compaction mode, then compare write amp and space amp.

---

## M7: Group commit, write stalls, failure handling (≈ 8 h)

There are no new `STRATA_TODO` sites. This milestone reworks `DBImpl::Write`
and the background error paths you already have.

**Implement:**
- A leader/follower writer queue: one WAL record and one `Sync` per group,
  with the DB mutex released during I/O.
- Write stalls at the L0 slowdown and stop triggers.
- The sticky background error (`include/strata/db.h`, "Errors (M7)").
- Clean shutdown under load.

**Tests:** `ctest -L m7` (group commit counts syncs; concurrency tests,
under TSan; the stall invariant; error handling). Gate:
`tools/check_milestone.sh M7 --tsan --asan`.

**Demo:** throughput vs threads, with durable writes.
```
for t in 1 2 4 8 16; do
  ./build/release/bench/strata_bench --benchmarks=fillsync,stats --num=20000 --threads=$t | grep -E 'fillsync|wal syncs'
done
./build/release/curriculum/labs/lab08_group_commit sweep     # the same idea, in miniature
```
The shape to show: ops/s scale roughly with threads while `wal syncs` stays
flat, and p99 latency stays bounded.

**C++ graded:**
- Condition-variable protocols: spurious wakeups, lost wakeups, and
  notify-under-lock or not.
- Lock scope discipline: prove the mutex is never held across fsync.
- Optional: Clang thread-safety annotations (`-Wthread-safety` with
  `GUARDED_BY`/`REQUIRES` on a small annotated mutex wrapper).
- Designing for TSan-clean code, not TSan-suppressed code.

**Rubric focus areas (must reach 4):** 4 error handling, 5 concurrency, 7 performance (measured).
**Prerequisites:** 07-fsync-semantics (fsyncgate), 08-wal-pattern (group commit).
**Commit:** `milestone 7: group commit, write stalls, sticky background errors`
**Stretch:** pipelined writes (WAL of group *n+1* overlaps memtable insert of group *n*).

---

## M8: Coroutines and async I/O (≈ 10 h)

**Implement:**
- `io/task.h`: `Task<T>` (lazy, symmetric transfer, exceptions, owns its
  frame), `SyncWait`, and `WhenAll`.
- `io/io_context.cc`: the blocking backend. Completions are queued, not
  resumed inline.
- `io/io_uring_context.cc` (Linux, in Lima): a RAII ring, SQ-full handling,
  and CQE reaping.

**Tests:** `ctest -L m8` (task semantics incl. 1M-iteration symmetric
transfer; IoContext on both backends). Gate: `tools/check_milestone.sh M8 --asan`,
run on macOS *and* in the Lima VM (for io_uring).

**Demo:** queue depth is the whole game.
```
# in the Lima VM, on the VM's own disk:
STRATA_BENCH_DIR=~/strata-data STRATA_BENCH_DIRECT=1 ./build/release/bench/bench_async_read
```
Blocking stays flat. io_uring scales with queue depth until the device
saturates. Tie it to `lab09_fio.sh` numbers for the same device.

**C++ graded:**
- Coroutines end to end: `promise_type`, awaiter protocol, `final_suspend`
  returning the continuation, `coroutine_handle` ownership (who destroys the
  frame, when), `std::exception_ptr`.
- Lifetime of objects captured by suspended coroutines (reference parameters!).
- RAII over a C API (`io_uring_queue_init`/`exit`).

**Rubric focus areas (must reach 4):** 1 ownership (coroutine frames), 2 value semantics, 6 generic programming.
**Prerequisites:** 06-io-uring, 05-io-paths.
**Commit:** `milestone 8: coroutine Task, WhenAll, blocking + io_uring backends`
**Stretch:** `Table::GetAsync` and a `DB::MultiGetAsync` that keeps one read in
flight per data block across many keys. Show the latency win over sequential Gets.

---

## After M8: make it a showcase

- `README.md` results section: architecture diagram, benchmark tables from
  your demos (M1 fsync, M3 memtable, M5 vs M6 reads, M6 write amp, M7 group
  commit scaling, M8 queue depth), and what you'd do next.
- A 1-page write-up of the hardest bug you hit, and how the tests or tools found it.
- Clean `clang-tidy` run; `ctest` green under all presets on both OSes.
