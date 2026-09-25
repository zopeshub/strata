# 03: Virtual memory

**Time:** ~1.5 h. **Runs on:** both (compare them). **Feeds:** M3, M4 (mmap reads).

## Concept

**Mapping, not memory.** A process sees a virtual address space. Page
tables map virtual pages to physical frames. The TLB caches those
translations. A page that isn't mapped yet causes a **page fault**, which the
kernel resolves in one of two ways:

- **Minor fault:** the data is already in RAM (a page-cache page, a zero
  page, a copy-on-write share). The kernel only has to wire up the page
  table entry. Microseconds, or less.
- **Major fault:** the data must be read from storage. The faulting thread
  blocks for a full device read (tens of µs on NVMe, ms on disk), and nothing
  in your code shows it.

**Kinds of mappings.** Anonymous memory (heap, stacks) is zero-filled on
first touch. File-backed `MAP_SHARED` mappings *are* the page cache: the
same physical pages `read(2)` would copy from. `MAP_PRIVATE` is
copy-on-write. `madvise` hints the access pattern: `MADV_SEQUENTIAL` gives
aggressive readahead, `MADV_RANDOM` gives none, and `MADV_WILLNEED` prefetches.
Memory is committed lazily: `malloc(1 GiB)` costs nothing until touched,
which is also why RSS and "allocated" differ.

**Page size is not a constant.** Apple Silicon macOS uses 16 KiB pages.
Linux on the same chip (your VM) uses 4 KiB. Anything you size to "a page"
(arena blocks, I/O alignment, SSTable blocks) should come from `sysconf`,
not a literal `4096`.

**mmap for databases, the debate.** mmap gives zero-copy reads and lets
the kernel manage caching. The costs (Crotty, Leis, Pavlo, CIDR '22): the
engine can't control eviction or know when an access will block. I/O errors
arrive as **`SIGBUS`** instead of an error code. Concurrent unmapping
triggers TLB shootdowns across cores. Page-table memory grows with the
mapped size. LMDB is built on mmap. LevelDB mmaps a bounded number of table files by
default (and `pread`s the rest). RocksDB defaults to `pread`, with mmap as
an option.

## Exercise: lab 03

`curriculum/labs/lab03_mmap_faults.cc`. Implement the page-touching loop
(`LAB_TODO`), then:

```bash
L=./build/debug/curriculum/labs/lab03_mmap_faults
$L create 1024; $L seq; $L seq; $L random; $L anon
```

Evict the file between runs (lab 04 shows how) and compare `seq` with
`seq-advise` and `random` with `random-advise`. Do it on macOS and in the
VM. Record fault counts and time per page. Explain: why is the second `seq`
run all minor faults? Why can `random` show *fewer* major faults than there
are pages, even cold?

## Checkpoint question

`PosixFileSystemOptions::use_mmap_reads` makes `Table` reads come straight
from a `MAP_SHARED` mapping. A reader thread is iterating an mmapped SSTable when
the device returns an I/O error for one of its pages. Describe what happens
in `pread` mode and in mmap mode, step by step, from the device to your
C++ code. How would you keep the mmap mode from killing the process? After
answering: would you make mmap reads the default?

## Engine tie-in

- **M1:** `PosixMmapReadableFile` (RAII over `mmap`/`munmap`) and the
  `FileSystem` option that chooses it.
- **M3:** the `Arena` hands out memory that is "allocated" long before it
  is touched. `ApproximateMemoryUsage` counts reserved bytes, not RSS.
  Think about which one the flush trigger should use.
- **M4:** the `Table` reader's zero-copy path: with mmap, `Read` can return
  a view into the mapping instead of copying into a scratch buffer.
  `bench/strata_bench --mmap_reads` measures the difference.

## Going deeper

- OSTEP ch. 18–23 (paging, TLBs, swapping).
- Crotty, Leis, Pavlo, *Are You Sure You Want to Use MMAP in Your Database
  Management System?*, CIDR 2022.
