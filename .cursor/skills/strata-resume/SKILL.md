---
name: strata-resume
description: Starts a strata tutoring session by reading PROGRESS.md, checking the repo state, and proposing the next concrete step. Use when the learner says /strata-resume, "where was I", or returns to the strata project after a break.
disable-model-invocation: true
---

# Resume a strata session

The learner may have been away for days. Reconstruct context from files,
not memory. Then hand them one concrete next step.

## Steps

1. Read `PROGRESS.md` completely. Note:
   - the current milestone
   - open questions
   - "Tutor observations"
   - the last session date
2. Check the repo state (run from `strata/`):
   ```bash
   git log --oneline -5
   git status --short
   tools/todo_report.sh M<current>        # remaining TODO sites for this milestone
   ```
3. If a build dir exists, run the current milestone's tests quietly:
   ```bash
   cmake --build --preset debug 2>&1 | tail -3
   ctest --preset debug -L m<current> 2>&1 | tail -5
   ```
   A missing build dir isn't an error. Tell them the configure command
   (`cmake --preset debug`).
4. Reply in this shape, under 15 lines:
   - **Where you are:** milestone, and N of M tests passing (or "not started").
   - **Last time:** one line from PROGRESS.md.
   - **Unanswered:** any open question or checkpoint the tutor asked earlier.
     Ask it again, verbatim.
   - **Next step:** one concrete, small thing. Examples: "read
     `curriculum/07-fsync-semantics.md` and answer its checkpoint",
     "make `tests/m1/coding_test.cc` green", "run the M2 demo".
5. If the prerequisite checkpoints for the current milestone (roadmap
   *Prerequisites*) aren't recorded as passed in PROGRESS.md, the next step
   is `/strata-checkpoint NN` for the first missing one.

Don't lecture, and don't pre-empt their questions. Wait for them to work.
