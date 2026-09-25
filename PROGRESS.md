# PROGRESS

The learner's persistent state. The tutor reads this first and updates it
last. Keep entries short, dated, and factual.

## Now

- **Current milestone:** M1 (not started)
- **Next step:** set your git identity (`git config user.name` /
  `user.email`). Read `docs/lsm-primer.md` and `docs/course-guide.md`, then
  do the M0 checks in `docs/roadmap.md`. Then read `curriculum/00-setup.md`,
  `01-filesystems.md`, and `07-fsync-semantics.md`, and do
  `/strata-checkpoint 01`.
- **Last session:** 2026-09-24: scaffold created (milestone 0 commit).

## Milestones

| M | Title | Status | Gate | Review | Commit |
|---|---|---|---|---|---|
| M0 | Scaffold | done | harness green | n/a | `milestone 0: scaffold` |
| M1 | Coding, CRC32C, RAII fds, POSIX FS | not started | | | |
| M2 | WAL, memtable, crash recovery | | | | |
| M3 | Arena + lock-free skiplist | | | | |
| M4 | SSTables + bloom | | | | |
| M5 | Flush, merged reads, iterators, snapshots, ranges | | | | |
| M6 | MANIFEST, versions, leveled compaction | | | | |
| M7 | Group commit, stalls, sticky errors | | | | |
| M8 | Coroutines + io_uring | | | | |

## Checkpoints

Format: `NN verdict (date): gap`. Topics 00–09 are in `curriculum/`.

_(none yet)_

## Hints used

Format: `M<n> <section> tier <k> (date)`.

_(none yet)_

## Demo outputs

Paste each milestone's demo output here (trimmed), with the machine and OS.

_(none yet)_

## Reviews

Format: `M<n> (date): verdict; scores 1..9; top open items`.

_(none yet)_

## Solutions given (tutor override)

_(none: keep it that way)_

## Open questions

Questions the tutor asked that are still unanswered, or things the learner
wants to come back to.

- (2026-09-24) Tutor's warm-up question, unanswered: with a move ctor
  that isn't `noexcept`, what does `std::vector<FileDescriptor>` do on
  reallocation, and what's the observable effect? How would a test prove it?
- (2026-09-24) Tutor's question for topic 07: on macOS, what has `fsync()`
  returning 0 actually promised?

## Tutor observations

Patterns noticed across sessions (strengths, recurring mistakes, what to
probe next time). Written by the tutor for the next tutor.

_(none yet)_

## Time log

| Date | Hours | What |
|---|---|---|
| 2026-09-24 | 0 | scaffold received |
