# strata

An LSM-tree key-value storage engine in modern C++ (C++23), built from first
principles as a learning and showcase project:

- a write-ahead log with crash recovery
- a lock-free-read skiplist memtable
- SSTables with bloom filters
- leveled compaction with a MANIFEST
- group commit
- a coroutine + io_uring read path

...all driven by tests that simulate power loss.

> **Status:** scaffold (milestone 0). The architecture, tests, tools, and
> benchmarks are in place. The engine is implemented milestone by
> milestone; see [`PROGRESS.md`](PROGRESS.md) and [`docs/roadmap.md`](docs/roadmap.md).

**New here?** Read these in order:

1. [`docs/lsm-primer.md`](docs/lsm-primer.md): what an LSM tree is and why
   it's built this way (30 min).
2. [`docs/course-guide.md`](docs/course-guide.md): how the curriculum,
   milestones, tests, hints, and tutor fit together.
3. [`docs/framework.md`](docs/framework.md): the tooling around the engine:
   the Lima VM, build presets, test harness, benchmarks, tracing tools, and
   the AI tutor layer (Cursor, OpenCode, or none).
4. [`docs/roadmap.md`](docs/roadmap.md): start at M0.

## Architecture

```
            Put/Delete/Write                              Get / NewIterator / Scan
                  │                                                 │
          ┌───────▼────────┐  group commit                          │
          │ writer queue   │──────────────┐                         │
          └───────┬────────┘              ▼                         │
                  │                 ┌───────────┐                   │
                  │                 │ WAL (log) │  32 KiB blocks,   │
                  │                 └───────────┘  CRC32C records   │
          ┌───────▼────────┐                                ┌───────▼────────┐
          │ memtable       │ skiplist on an arena           │ merging        │
          │ (mutable)      │ lock-free reads                │ iterator       │
          ├────────────────┤                                │ mem→imm→L0→Ln  │
          │ imm memtable   │──flush──┐                      └───────▲────────┘
          └────────────────┘         ▼                              │
                          ┌─────────────────────┐  bloom + index    │
                          │ L0  SST SST SST     │  blocks in memory │
                          │ L1  SST SST         │───────────────────┘
                          │ L2  SST SST SST ... │  leveled compaction
                          └─────────────────────┘  (background thread)
                   MANIFEST (VersionEdits) + CURRENT: which files are live
```

Details: [`docs/design.md`](docs/design.md), covering formats, the recovery
protocol, compaction rules, and the concurrency and error models.

## Build and test

Requirements: CMake ≥ 3.25, Ninja, and a C++23 compiler with
`std::expected`. Tested with Apple clang 21 (macOS) and GCC 14 (Ubuntu
24.04). GoogleTest and Google Benchmark are fetched automatically. On
Linux, `liburing` enables the io_uring backend.

```bash
cmake --preset debug && cmake --build --preset debug
ctest --preset debug -L harness          # test infrastructure
ctest --preset debug -L m1               # milestone 1's spec
tools/check_milestone.sh M1 --asan       # the milestone gate
```

Presets: `debug`, `release`, `asan` (ASan+UBSan), `tsan`, and `strict`
(`-Werror -Wconversion`).

## Tools

| Tool | Purpose |
|---|---|
| `strata_cli` | interactive shell: put/get/del/scan/flush/compact/properties |
| `strata_wal_dump`, `strata_sst_dump` | inspect WAL records and SSTable blocks, stats, bloom behavior |
| `tools/crash_test.sh` | `kill -9` loop with a verifier: acknowledged writes must survive |
| `strata_bench` | db_bench-style workloads (fillrandom, readrandom, fillsync --threads, ...) with latency histograms and write amplification |
| `bench/bench_*` | micro-benchmarks: fsync methods, CRC32C, memtable, bloom, block, merge, async reads |

## Repository layout

```
include/strata/   public API        src/       engine modules (util fs db table io)
tests/            spec per milestone + MemFileSystem power-loss simulator
bench/ tools/     benchmarks, CLI, dumpers, crash test, milestone gate
curriculum/       OS fundamentals for storage (filesystems → io_uring → SSDs) with labs
docs/             primer, course guide, framework, design, roadmap, C++ rubric, hints, ADRs
lima/             Linux VM definition (io_uring, O_DIRECT, dm-flakey, tracing)
.cursor/ .opencode/ opencode.json   AI tutor rules + commands (Cursor, OpenCode)
```

## Results

_Filled in as milestones land: fsync costs per method and OS, memtable
scaling, read latency vs L0 depth, write amplification, group-commit
scaling, and io_uring queue-depth scaling._
