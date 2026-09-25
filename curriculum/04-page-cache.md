# 04: The page cache

**Time:** ~1.5 h. **Runs on:** both. **Feeds:** M4, M5, benchmarks.

## Concept

**One cache for everything.** On Linux and macOS, file data lives in the
*page cache*: physical pages indexed by (inode, offset). `read(2)` copies
from it, `write(2)` copies into it and marks pages dirty, and `mmap` maps its
pages directly. So a "disk read" in a benchmark is usually a `memcpy` from
RAM, unless the data isn't cached.

**Read path.** For each page in the requested range, `read` looks it up in
the cache. On a miss, it allocates a page and submits a read, plus
**readahead**: the kernel notices sequential access and reads further ahead
asynchronously, growing the window. Random 4 KiB reads get no readahead,
or they get it and waste bandwidth. `posix_fadvise` (Linux) or
`F_RDAHEAD`/`F_RDADVISE` (macOS) tune it.

**Write path and writeback.** Dirty pages are written back by background
threads when they age (`dirty_expire_centisecs`, ~30 s), when too much of
memory is dirty (`dirty_background_ratio`), or on `fsync`. Writers that
dirty pages faster than the device can absorb them are **throttled**
inside `write(2)` (`balance_dirty_pages`). That's why a buffered write can
suddenly take 100 ms.

**Eviction.** Roughly LRU with two lists (active/inactive), or MGLRU on
newer kernels. A large one-time scan (a backup, or a compaction!) can push
your hot data out. `POSIX_FADV_DONTNEED` drops a file's clean pages.

**Bypassing it.** `O_DIRECT` (Linux) reads and writes device-aligned
buffers directly, with no caching and no readahead. Buffers, offsets, and
lengths must be aligned to the logical block size. macOS has
`fcntl(F_NOCACHE)`, which is advisory, not identical. Databases with their
own buffer pool (Postgres doesn't; InnoDB and RocksDB optionally do) use
direct I/O to avoid **double caching**: the same block in the engine's
cache and the kernel's.

## Exercise: lab 04

`curriculum/labs/lab04_page_cache.cc`. Implement `ResidentFraction`
(`mmap` + `mincore`) and `Evict` (Linux: `posix_fadvise(DONTNEED)`; macOS:
think about it and read the file's header comment). Then run this sequence
and explain every number:

```bash
L=./build/debug/curriculum/labs/lab04_page_cache
$L create 512; $L resident; $L read; $L resident; $L evict; $L resident; $L read; $L read-direct
```

Put the p50/p99 for warm, cold, and direct reads in `curriculum/NOTES.md`.
You'll need them for the checkpoint and for M4's benchmark discussion.

## Checkpoint question

strata has no block cache: every SSTable data-block read is a `pread`
served by the page cache. Using your lab 04 numbers, estimate the latency of
a `Get` that finds its key in the third L0 file when everything is cached,
and when nothing is. What do bloom filters change in each case? Then: RocksDB
adds a user-space LRU block cache and often runs with `O_DIRECT`. What does
that gain, what does it cost, and what would you have to measure before
adding one to strata?

## Engine tie-in

- **M4:** `Table::Get` = bloom check (in memory) → index lookup (in
  memory) → **one** data-block `pread`. That one read is page cache or
  device, and your whole read latency distribution depends on which.
  `Table` counts block reads so tests and benchmarks can see it.
- **M5/M6:** flush and compaction write large sequential files and read
  them back once. Consider `POSIX_FADV_DONTNEED` on compaction inputs after
  they're deleted, and readahead hints on compaction reads. This is a
  stretch goal in the roadmap.
- **Benchmarks:** `strata_bench --benchmarks=readrandom` on a DB larger
  than RAM vs smaller than RAM are two different experiments. Say which
  one you ran in every result you quote.

## Going deeper

- Linux `Documentation/admin-guide/sysctl/vm.rst` (the `dirty_*` knobs).
- Brendan Gregg, *Systems Performance*, 2nd ed., ch. 8 (File Systems).
