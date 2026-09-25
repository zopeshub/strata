# How this course works

strata is two things at once: a **course** (OS fundamentals for storage,
plus intermediate/advanced C++) and a **project** (an LSM storage engine you
can show people). This page explains how the pieces fit together. Read
it once. After that, `docs/roadmap.md` is your day-to-day guide.

## The goal

By the end you'll have written, yourself, a crash-safe, concurrent LSM
engine (~5–8k lines of C++23) with benchmarks. You'll be able to explain
every durability, memory-model, and design decision in it. The C++ is
graded as seriously as the correctness.

## The pieces

| Piece | Where | Role |
|---|---|---|
| **Primer** | `docs/lsm-primer.md` | why LSM trees look the way they do (read first) |
| **Curriculum** | `curriculum/00`–`09` | OS theory, one lab, and one checkpoint question per topic |
| **Roadmap** | `docs/roadmap.md` | 8 milestones: what to build, tests, gate, demo, C++ focus, commit message |
| **Design doc** | `docs/design.md` | the exact formats and protocols (the reference while you code) |
| **Skeleton** | `src/`, `include/` | every function exists; the logic is `STRATA_TODO("Mx", ...)` |
| **Tests** | `tests/m1`–`m8` | the executable spec; a red test names the TODO to write |
| **Hints** | `docs/hints/m1`–`m8.md` | three tiers per problem: nudge, direction, near-pseudocode |
| **Gate** | `tools/check_milestone.sh` | objective "done": no TODOs left, a strict build, all tests to date |
| **Rubric** | `docs/cpp-rubric.md` | nine C++ areas scored 1–5; the milestone's focus areas must reach 4 |
| **Tools** | `tools/`, `bench/` | dumpers, CLI, crash tester, benchmarks: how you *show* it works |
| **Progress** | `PROGRESS.md` | your state: milestone, checkpoints, hints used, demos, reviews |
| **Tutor** | `.cursor/` | AI tutor rules and skills; `.cursor/tutor/` is tutor-only (don't read) |

## The loop (every milestone)

```
 read the prerequisite curriculum topics ──► do their labs ──► /strata-checkpoint NN
                                                                     │
      ┌──────────────────────────────────────────────────────────────┘
      ▼
 tools/todo_report.sh M<n> ──► implement ──► ctest -L m<n> ──► red? ──► /strata-hint (one tier)
                                   ▲                             │
                                   └─────────────────────────────┘
      │ all green
      ▼
 tools/check_milestone.sh M<n> [--asan/--tsan] ──► PASS
      ▼
 run the demo, and paste it into PROGRESS.md ──► write an ADR ──► commit "milestone <n>: ..."
      ▼
 /strata-review M<n> ──► fix what it finds ──► DONE
```

## Milestones at a glance

| M | You build | OS topics | C++ you'll be graded on | Demo | Hours |
|---|---|---|---|---|---|
| 1 | encoding, CRC32C, RAII fds, POSIX file system | 00, 01, 07 | RAII, move semantics, `std::expected`, UB-free byte handling | fsync cost per method, macOS vs Linux | 6–8 |
| 2 | WAL, memtable (`std::map`), recovery | 02, 08 | `std::variant`/`visit`, type erasure, heterogeneous lookup | kill -9 loop: nothing acknowledged is lost | 12 |
| 3 | arena + lock-free-read skiplist | 03 | **memory model**: acquire/release, placement new, lifetimes | memtable scaling before and after | 8–10 |
| 4 | SSTable builder, reader, and bloom filters | 04, 03 | pimpl, `string_view` lifetimes, parsing untrusted bytes | bloom filter skipping reads | 10 |
| 5 | flush, merged reads, iterators, snapshots | 05 | C++20 ranges, `shared_ptr` deleters, threads + CVs | read latency grows with L0 | 12 |
| 6 | MANIFEST, versions, leveled compaction | 02, 09 | immutable shared state, RAII guards, strong exception safety | write amplification + crash torture | 16 |
| 7 | group commit, stalls, sticky errors | 07, 08 | condition-variable protocols, lock discipline, TSan-clean | throughput vs threads, one fsync per group | 8 |
| 8 | coroutine `Task`, io_uring backend | 06, 05 | coroutines end to end, RAII over a C API | queue depth scaling | 10 |

About 85 hours of milestones plus ~15 of curriculum. The time log in
`PROGRESS.md` is for your actuals.

## Working with the tutor

The AI tutor is **Socratic by design**. It asks questions, points at tests
and lines, and grades your C++. **It won't write your code**: that's a
rule in the repo (`.cursor/rules/strata-tutor.mdc`), not a mood. If you
truly want code, say "tutor override". It'll comply, log it in
`PROGRESS.md`, and cap that area's score at 3.

| Command | When |
|---|---|
| `/strata-resume` | start of every session (it reads PROGRESS.md and tells you where you are) |
| `/strata-checkpoint NN` | after reading a curriculum topic |
| `/strata-hint M<n>` | stuck for 20+ minutes |
| `/strata-review M<n>` | the gate passes and the demo is done |

The tutor's memory is `PROGRESS.md`, not the chat. Any model in any session
can pick up where the last one stopped.

## Working without any AI

Everything objective works offline:

- the tests tell you what's wrong
- the gate tells you when you're done
- the hints unblock you
- the rubric's anchors and probe questions let you grade your own C++

The answer key in `.cursor/tutor/` becomes your self-check. Read an entry
only *after* you've written down your own answer to that checkpoint.

## Ground rules that make it work

1. **Tests are the spec.** Don't edit a test to make it pass. If you think
   one is wrong, write down why, and keep it red until you've convinced yourself.
2. **Predict before you run** (labs and benchmarks). Wrong predictions are the lesson.
3. **One hint tier at a time**, after 20 minutes of trying.
4. **Every milestone ends with something you can show**: a demo output, an
   ADR, a commit. The README's Results section fills in as you go.
