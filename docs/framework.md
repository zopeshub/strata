# The framework: tools, VM, and how it all fits together

This page covers everything *around* the engine: the machines, build
system, test harness, benchmarks, observability tools, and the AI tutor
layer. It says what each piece is, why it's here, and how the pieces
connect. For the engine itself, see [`design.md`](design.md). For the
course flow, see [`course-guide.md`](course-guide.md).

---

## 1. The big picture

```
┌──────────────────────────── macOS host (your Mac) ─────────────────────────────┐
│                                                                                │
│  Editor + AI agent (Cursor / OpenCode / ...)                                   │
│     reads AGENTS.md, .cursor/rules, skills, PROGRESS.md                        │
│                                                                                │
│  ~/Documents/cursor_workspace/strata   ← the one source of truth (git repo)    │
│     │  cmake --preset debug   (Apple clang, libc++)                            │
│     │  ctest / check_milestone.sh / labs 01,03,04,05,07,08,09                  │
│     │                                                                          │
│     │ virtiofs mount (same absolute path inside the VM)                        │
│     ▼                                                                          │
│  ┌──────────────────────── Lima VM "strata" (Ubuntu 24.04) ─────────────────┐  │
│  │  /Users/<you>/Documents/cursor_workspace/strata   (mounted, don't build) │  │
│  │        │ tools/vm_sync.sh (rsync)                                        │  │
│  │        ▼                                                                 │  │
│  │  ~/strata   on the VM's own ext4 disk: build + experiment here           │  │
│  │     gcc-14 / clang-18, liburing (io_uring), strace, bpftrace, perf,      │  │
│  │     fio, dm-flakey, drop_caches, O_DIRECT   → labs 02, 05, 06; M8 gate   │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────────────┘
```

**You edit in one place (the Mac), and build and test in two places.**
macOS catches portability bugs and teaches its own durability lessons
(`F_FULLFSYNC`, 16 KiB pages). Linux is where io_uring, `O_DIRECT`,
dm-flakey, and block-layer tracing live.

On a Linux machine, the VM box *is* your machine: skip Lima, build in
place. See [Running on native Linux](#11-running-on-native-linux).

---

## 2. Lima: a Linux VM for your Mac

[Lima](https://lima-vm.io) ("Linux Machines") is a CLI that creates and runs
Linux VMs on macOS, with the conveniences of WSL2: automatic file sharing
with the host, SSH access, and port forwarding. It's open source (a CNCF
project), and it's also the engine behind Colima and Rancher Desktop.

How it works:

- **Hypervisor:** `vmType: vz` uses Apple's **Virtualization.framework**
  (native, fast on Apple Silicon). The alternative is `qemu`.
- **Image:** a stock Ubuntu **cloud image**, downloaded once and cached.
- **Provisioning:** on first boot, **cloud-init** runs the `provision` scripts
  from the YAML (installing packages and so on).
- **Access:** `limactl shell strata` is SSH under the hood.
- **File sharing:** host directories listed under `mounts` appear inside
  the VM via **virtiofs**, at the same absolute path as on the Mac.

Commands you'll use:

```bash
limactl start --name=strata lima/strata.yaml   # create + first boot (once, ~5 min)
limactl start strata                           # boot it again later
limactl shell strata                           # a shell inside
limactl shell strata tools/vm_sync.sh          # run one command inside (cwd = same path)
limactl stop strata                            # free its 6 GiB RAM
limactl list                                   # status
limactl delete strata                          # throw it away (the YAML recreates it)
```

The VM is disposable: everything that matters is in the YAML and the repo.

---

## 3. `lima/strata.yaml`, block by block

| Block | What it says | Why |
|---|---|---|
| `vmType: vz` | use Apple Virtualization.framework | native speed on Apple Silicon; virtiofs support |
| `images:` | Ubuntu 24.04 cloud images for arm64 and x86_64 | Lima picks the one matching your Mac; 24.04 ships GCC 14, clang 18, a 6.8 kernel (modern io_uring) |
| `cpus: 4`, `memory: 6GiB`, `disk: 40GiB` | VM size | enough for parallel builds, sanitizers, a few GiB of benchmark data, and fio files |
| `mounts:` | `~/Documents/cursor_workspace`, writable | lets the VM see the repo. **Anyone else using this repo should change this path to where they cloned it.** |
| `provision:` (mode `system`) | a root shell script run by cloud-init on first boot | installs the toolchain (below) and sets `gcc` → gcc-14 |
| `perf_event_paranoid = 1` | a sysctl | lets `perf` read counters without sudo |
| `probes:` | waits until `g++-14`, `cmake` and `liburing.h` exist | `limactl start` doesn't report "ready" until provisioning has actually finished |
| `message:` | printed after boot | reminds you to sync the repo onto the VM disk |

What the provision script installs, and which part of the course uses it:

| Package(s) | Used for |
|---|---|
| `build-essential gcc-14 g++-14 clang-18 lld-18 clang-tidy-18 cmake ninja-build pkg-config` | building strata with a second compiler family |
| `liburing-dev` | io_uring: lab 06 and the M8 backend (CMake detects it) |
| `strace ltrace` | syscall tracing (lab 05) |
| `bpftrace linux-tools-*` (perf) `sysstat` (iostat) `blktrace` | block-layer and kernel tracing (lab 05) |
| `fio` | device characterization (lab 09) |
| `util-linux e2fsprogs xfsprogs` | `losetup`, `mkfs.ext4/ext2/xfs`, `dmsetup` for dm-flakey (lab 02) |
| `gdb valgrind` | debugging |
| `rsync` | `tools/vm_sync.sh` |
| `ripgrep` | `todo_report.sh` and the gate's TODO count |

### Why not build on the mount?

The virtiofs mount is great for *seeing* files, but wrong for storage
experiments. Its caching and `fsync` semantics are the Mac's, filtered
through the hypervisor, so `fsync` latency, page-cache behavior, and
`O_DIRECT` results would describe virtiofs, not Linux. It's also slow for
builds, and a macOS `build/` and a Linux `build/` in the same directory
would collide. So `tools/vm_sync.sh` rsyncs the repo to `~/strata` on the
VM's own ext4 disk, excluding `build/`. Re-run it after editing on the Mac.

Even the VM's disk is a file on your Mac's APFS volume. Semantics (which
syscalls, in which order) are real Linux. Latencies pass through the
hypervisor. Topic 00's checkpoint question is about exactly this.

---

## 4. Build system

| Piece | Choice | Notes |
|---|---|---|
| Language | **C++23** | `std::expected` (`Result<T>`), `std::byteswap`, ranges, coroutines |
| Build | **CMake ≥ 3.25 + Ninja** | `CMakeLists.txt`, `cmake/StrataOptions.cmake` (warnings, sanitizers), `cmake/StrataDependencies.cmake` |
| Presets | `CMakePresets.json` | each preset builds into `build/<preset>/`; the same names work for `cmake --build --preset` and `ctest --preset` |
| Dependencies | **FetchContent**, pinned tarballs | GoogleTest 1.17.0, Google Benchmark 1.9.4. There's no system install to manage |
| Feature detection | `pkg-config liburing` | on Linux it sets `STRATA_HAVE_IO_URING`, which compiles the io_uring backend and lab 06 |
| Tooling output | `compile_commands.json` | for clang-tidy and editor code intelligence |

| Preset | For |
|---|---|
| `debug` | day-to-day: fast builds, assertions |
| `release` | benchmarks and demos (`-O2`); debug-build numbers are meaningless |
| `asan` | AddressSanitizer + UndefinedBehaviorSanitizer: memory errors, UB |
| `tsan` | ThreadSanitizer: data races (the M3 and M7 gates require it) |
| `strict` | `-Werror -Wconversion -Wsign-conversion`, benchmarks off. The gate always builds this |

Warnings apply only to strata's own targets, so third-party code doesn't
drown the signal. Sanitizers apply globally, because they need everything
instrumented.

---

## 5. Test harness

**GoogleTest**, one executable per test file, grouped by milestone:

- `tests/m1/` … `tests/m8/` are the spec. `tests/CMakeLists.txt` labels
  every discovered test case with its milestone (`m1` … `m8`), so
  `ctest --preset debug -L m3` runs exactly M3. The `harness` label covers
  the test infrastructure's own tests, which pass from day one.
- **How a red test points at your TODO:** every unimplemented function
  calls `STRATA_TODO("M2", "what")` (`include/strata/todo.h`). It throws
  `NotImplementedError` carrying the milestone, file, and line, and gtest
  prints it as the failure. In `noexcept` contexts (destructors, moves),
  `STRATA_TODO_NOEXCEPT` prints the same message and aborts, which ctest
  shows as "Subprocess aborted".

The interesting parts are in `tests/support/`:

| Piece | What it does |
|---|---|
| **`MemFileSystem`** (`mem_fs.h`) | an in-memory `FileSystem` with **POSIX-strict durability**: it tracks, per file and per directory entry, what has been synced. `SimulatePowerLoss(Unsynced::kDropAll or kKeepRandomPrefix)` throws away everything un-synced, including un-synced *file names* (the missing directory fsync bug). `FailNextSyncs(n)` injects EIO. `SetSyncLatency` makes fsync slow (for group-commit tests). `Freeze`/`Unfreeze` model "the machine stops here". This is how tests simulate **power loss** thousands of times per second without real hardware. |
| **Model tests** (`db_model.h`) | random operation sequences (puts, deletes, batches, snapshots, flushes, compactions, reopens) applied to the DB *and* to a `std::map`, with results compared after every step. Failures print the seed. Replay one with `STRATA_MODEL_SEED=<n>`. |
| **Crash torture** (`tests/m6/db_crash_test.cc`) | power loss at random points during flushes and compactions, crashes during recovery, and double crashes, checking the **prefix property** every time. Replay with `STRATA_CRASH_SEED=<n>`. |
| `thread_util.h` | `RunConcurrently` (exceptions propagate out of threads) and a scope guard |
| `STRATA_KEEP_TEST_DIRS=1` | keeps the temp directories of POSIX-backed tests for inspection |

**Real process crashes** complement the simulation: `tools/crash_test.sh`
runs `strata_crash_writer`, kills it with `SIGKILL` at a random moment,
runs `strata_crash_verify`, and repeats. That exercises a *process* crash
(the page cache survives). Power loss is what `MemFileSystem` and the
dm-flakey lab are for.

---

## 6. Benchmarks

| Tool | What | Run |
|---|---|---|
| `bench/bench_*` | **Google Benchmark** micro-benchmarks: `bench_coding`, `bench_crc32c`, `bench_fsync` (sync methods × sizes; `STRATA_BENCH_DIR` picks the disk), `bench_memtable`, `bench_bloom` (measured FPR), `bench_block`, `bench_merge`, `bench_async_read` (`STRATA_BENCH_DIRECT=1` for O_DIRECT) | `./build/release/bench/bench_fsync --benchmark_filter=Append` |
| `strata_bench` | a **db_bench-style** whole-engine benchmark: `fillseq`, `fillrandom`, `overwrite`, `fillsync`, `readrandom`, `readmissing`, `readseq`, `compact`, `stats`. It has `--threads`, exact latency percentiles, and reports **write amplification** | `./build/release/bench/strata_bench --benchmarks=fillrandom,readrandom,stats --num=1000000` |

Keep curated results in `bench/results/` (text). Always use the `release`
preset, and say which machine and OS each number came from.

---

## 7. Project tools (`tools/`)

| Tool | Purpose |
|---|---|
| `check_milestone.sh M<n> [--asan] [--tsan] [--tidy]` | **the gate**: no TODOs left, a strict build, every test from M1 to M*n*, optional sanitizers and clang-tidy. Prints PASS or NOT DONE with reasons |
| `todo_report.sh [M<n>]` | TODO counts per milestone, or every site for one milestone (`file:line: what`) |
| `vm_sync.sh` | copies the repo from the virtiofs mount to `~/strata` on the VM disk (run inside the VM) |
| `crash_test.sh` | the `kill -9` torture loop (M2 and M6 demos) |
| `strata_cli` | an interactive REPL on a real DB: `put`, `get`, `del`, `scan`, `flush`, `compact`, `prop`, `snap` |
| `strata_wal_dump` | decodes a WAL: records, batches, corruption (`--raw`, `--no-verify`) |
| `strata_sst_dump` | decodes an SSTable: blocks, stats, `--get` with bloom and block-read counts, and `--build=N` to make a demo table |

---

## 8. OS observability tools (used in the labs)

| Tool | OS | Shows | Lab |
|---|---|---|---|
| `strace -T` | Linux | every syscall and its latency | 05 |
| `bpftrace` | Linux | kernel tracepoints (block I/O issue/complete, flags such as FLUSH/FUA) | 05 |
| `perf trace` / `perf stat` | Linux | syscall summaries, CPU counters | 05 |
| `fs_usage` | macOS | file-system calls per process | 05 |
| `fio` | both | device IOPS, bandwidth, and latency percentiles | 09 |
| `dmsetup` + dm-flakey | Linux | simulated power loss on a real file system | 02 |
| `mincore`, `posix_fadvise`, `/proc/sys/vm/drop_caches`, `purge` | both | what's in the page cache; evicting it | 03, 04 |
| `getrusage` | both | minor and major page faults | 03 |

---

## 9. Code quality

| Tool | Config | Enforced by |
|---|---|---|
| clang-format | `.clang-format` (Google-based, 100 columns) | convention, plus review. Run `clang-format -i` on your files |
| clang-tidy | `.clang-tidy`: `bugprone`, `performance`, `concurrency`, `cppcoreguidelines`, `modernize`, `readability`. A curated subset (use-after-move, dangling handles, unchecked optional access, noexcept moves, and so on) are **errors** | `check_milestone.sh --tidy` (on macOS it uses Homebrew's `llvm`) |
| Sanitizers | `asan`, `tsan` presets | gate flags per milestone |
| Warnings | always `-Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast ...`; `strict` adds `-Wconversion -Wsign-conversion -Werror` | the `strict` preset in every gate run, on both compilers |

---

## 10. The AI tutor layer

The tutor is **plain files in the repo**: no plugin, no service. Any
capable model that reads them behaves the same way.

| File | Role |
|---|---|
| `AGENTS.md` | entry point for *any* agent: "you are a tutor here", key commands, map |
| `.cursor/rules/strata-tutor.mdc` | the tutor contract: Socratic, never write TODO code, read and update PROGRESS.md, the gate before opinions |
| `.cursor/rules/strata-cpp-review.mdc` | how to critique C++ (active when C++ files are in context) |
| `.cursor/skills/strata-{resume,hint,checkpoint,review}/SKILL.md` | step-by-step procedures behind the four slash commands |
| `.cursor/tutor/` | answer key and review checklists (tutor-only) |
| `docs/cpp-rubric.md`, `docs/hints/` | the shared grading standard and hint ladders |
| `PROGRESS.md` | **the tutor's memory**: milestone, checkpoint verdicts, hints used, reviews, observations for the next session |

Session state never lives in a chat. It lives in `PROGRESS.md`, so any
model in any tool, days later, can continue.

### Cursor

It works out of the box when you open `strata/` (or the parent workspace,
through its pointer rule and skill stubs). Rules with `alwaysApply` load
every session. The C++ review rule loads when matching files are in
context. `/strata-review M1` and the other commands load the skills on demand.

### OpenCode

It works too. [OpenCode](https://opencode.ai) is an open-source terminal
coding agent that works with many model providers. What it picks up:

| OpenCode feature | strata file | Effect |
|---|---|---|
| project rules: `AGENTS.md` (auto-loaded) | `AGENTS.md` | tutor mode and project map |
| `instructions` in `opencode.json` | `opencode.json` → `.cursor/rules/*.mdc` | the same tutor and review rules Cursor uses |
| custom commands: `.opencode/commands/*.md` | `.opencode/commands/strata-*.md` | `/strata-resume`, `/strata-hint M2`, `/strata-checkpoint 07`, `/strata-review M1`, each telling the agent to follow the matching `SKILL.md` |

Run `opencode` from inside `strata/`. The commands take arguments
(`$ARGUMENTS`), so `/strata-review M3` works the same as in Cursor.

### Anything else

Other agents that read `AGENTS.md` (many do) get tutor mode automatically.
For a plain chat model, paste `AGENTS.md` and
`.cursor/rules/strata-tutor.mdc`, then the relevant `SKILL.md`. For no AI
at all, see "Working without any AI" in [`course-guide.md`](course-guide.md).

---

## 11. Running on native Linux

Everything works on a Linux machine, with no VM. The Lima VM is plain
Ubuntu 24.04, so native Ubuntu 24.04 runs the exact same setup. Tested
there: strict (`-Werror`) builds with GCC 13 (Ubuntu's default) and GCC 14,
the io_uring backend and labs, `todo_report.sh`, and `check_milestone.sh`.

**Install** (Ubuntu 24.04 or newer; the same list the VM provisions):

```bash
sudo apt install build-essential cmake ninja-build pkg-config git ripgrep \
                 liburing-dev                                    # required
sudo apt install gcc-14 g++-14 clang-18 clang-tidy-18 \
                 fio strace ltrace sysstat blktrace bpftrace gdb valgrind \
                 util-linux e2fsprogs xfsprogs \
                 linux-tools-common "linux-tools-$(uname -r)"    # labs, --tidy
```

Then build in place, straight from your clone:

```bash
cmake --preset debug && cmake --build --preset debug
ctest --preset debug -L harness      # green from day one
tools/check_milestone.sh M1
```

Ignore `lima/` and `tools/vm_sync.sh`. When a doc says "inside the VM",
read it as "in your terminal".

**Requirements:** CMake ≥ 3.25, and GCC ≥ 13 or clang ≥ 18 (for C++23
`std::expected`). Ubuntu 22.04 ships CMake 3.22 and GCC 11/12, so there you
need a newer CMake (Kitware's apt repo, or `pip install cmake`) and GCC 13
(the `ubuntu-toolchain-r/test` PPA). Other distros (Fedora, Arch, Debian 13)
have new enough packages under similar names. They're untested.

**What's different from the Mac setup:**

| | |
|---|---|
| **Better** | latency numbers are real: `fsync`, `O_DIRECT`, and `fio` hit your actual disk, with no hypervisor in between. Your topic 07 and 09 results mean more than the VM's |
| **Lost** | the macOS half of the comparisons: `F_FULLFSYNC` vs `fsync` (topic 07, the M1 demo), `fs_usage`, 16 KiB pages, APFS. Read those parts, and treat the macOS numbers in the text as given. You still write the `__APPLE__` branches (M1's `SyncFd`, lab 07), but your machine never compiles them, so have a reviewer (or a Mac CI runner) check them |
| **Careful** | labs 02 (dm-flakey), 05 (`bpftrace`), and 06 (`drop_caches`) need `sudo`. Run dm-flakey only on a **loop device over a scratch file**, as the lab says, never on a real disk. On a laptop, the fsync benchmarks wear your SSD a little. That's fine, but don't loop them for hours |
| **Check** | io_uring can be disabled by policy: `sysctl kernel.io_uring_disabled` should print 0 (the setting doesn't exist before kernel 6.6, which is fine). Containers often block it too (Docker's default seccomp profile) |

**Windows:** WSL2 runs Ubuntu, so the steps above should work there
(build under `~`, not `/mnt/c`). Untested; dm-flakey and `perf` may not
be available in WSL2's kernel.
