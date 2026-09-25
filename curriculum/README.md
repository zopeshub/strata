# OS fundamentals, storage flavor

Ten short topics. Each has one concept explainer (~10 minutes of reading),
one hands-on lab (30–90 minutes), and one checkpoint question you answer
**out loud to the tutor** (`/strata-checkpoint NN`) before starting the
milestone that depends on it. Each topic also says exactly where it shows
up in the engine.

This is not a textbook. The explainers are compressed and assume you know
what a syscall is. Every topic ends with the one or two primary sources
worth your time if you want depth.

| # | Topic | Lab | Runs on | Feeds |
|---|---|---|---|---|
| 00 | [Setup](00-setup.md) | toolchain + Lima VM | both | M0 |
| 01 | [Filesystems](01-filesystems.md) | `lab01_atomic_rename` | macOS | M1, M5, M6 |
| 02 | [Crash consistency](02-crash-consistency.md) | `lab02_durable_create` + dm-flakey | **Linux** | M2, M5, M6 |
| 03 | [Virtual memory](03-virtual-memory.md) | `lab03_mmap_faults` | both | M3, M4 |
| 04 | [The page cache](04-page-cache.md) | `lab04_page_cache` | both | M4, M5 |
| 05 | [I/O paths](05-io-paths.md) | `lab05_io_path_probe` + tracing | both (tracing best on Linux) | M5, M8 |
| 06 | [io_uring](06-io-uring.md) | `lab06_uring_read` | **Linux** | M8 |
| 07 | [fsync semantics](07-fsync-semantics.md) | `lab07_fsync_latency` | both | M1, M7 |
| 08 | [The WAL pattern](08-wal-pattern.md) | `lab08_group_commit` | both | M2, M7 |
| 09 | [SSD internals](09-ssd-internals.md) | `lab09_fio.sh` | both | M6, benchmarks |

## How to use it

- **Order:** follow the roadmap's *Prerequisites* lines, not this table.
  For M1 you need 00, 01, and 07. For M2, 02 and 08. Topics are
  independent enough to read out of order.
- **Labs have `LAB_TODO` markers**, just like the engine. They're small:
  usually one function, where the whole point of the lab is that you write
  the syscall sequence yourself. Build them with the rest of the repo
  (`cmake --build --preset debug`). They're in `build/debug/curriculum/labs/`.
- **Record numbers** in `curriculum/NOTES.md` (create it; it's yours).
  Several milestone demos quote them ("F_FULLFSYNC costs 4 ms on this
  laptop, so group commit matters").
- **Predict before you run.** Every lab asks you to write down a prediction
  first. A wrong prediction you correct is worth more than a right number you
  didn't think about. Put predictions in NOTES.md too.
- **Checkpoint questions** have no answer in this directory, on purpose.
  Answer in your own words to the tutor. It probes, doesn't grade, and
  won't give you the answer unless you've made a real attempt.

## Where labs write files

`$LAB_DIR`, default `/tmp`. **In the Lima VM, always use a directory on the
VM's own disk** (`export LAB_DIR=~/lab`), never the virtiofs mount of your
Mac workspace: its caching and durability semantics are the host's,
filtered through the virtualization layer, and they make every storage
experiment meaningless.

## Primary sources, used throughout

- *Operating Systems: Three Easy Pieces* (Arpaci-Dusseau), free online:
  the persistence chapters (36–45).
- Pillai et al., *All File Systems Are Not Created Equal*, OSDI 2014: the
  paper to read if you only read one.
- Linux `Documentation/filesystems/vfs.rst` and the `fsync(2)`, `rename(2)`,
  `io_uring(7)` man pages. Actually read the man pages.
