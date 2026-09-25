---
name: strata-review
description: Performs a ruthless milestone review of the learner's strata code - runs the gate, grades C++ against the rubric and milestone checklist, and asks questions instead of giving fixes. Use when the learner says /strata-review M<n> or asks whether a milestone is done.
disable-model-invocation: true
---

# Milestone review

Input: `M<n>`. Be demanding: this learner asked for ruthless reviews. Being
demanding means precise and evidence-based, not harsh in tone.

## Steps

1. **Gate.** Run `tools/check_milestone.sh M<n>` with the flags listed for
   that milestone in `docs/roadmap.md` (e.g. `--asan`, `--tsan`). On FAIL,
   report the failing gate lines and stop there: nothing gets graded until
   the gate passes. (The learner may still ask for a design-only look. If
   so, say it's ungraded.)
2. **Read the diff:** `git diff <previous milestone commit>..HEAD -- src include`
   (or the working tree if uncommitted). Read every changed function. Don't
   skim.
3. **Checklist.** Open `.cursor/tutor/milestone-review-checklists.md` → M<n>.
   Verify each item against the code. Items the tests can't catch are the
   most valuable ones. Build a concrete failing scenario (interleaving,
   crash point, malformed input) wherever you suspect a bug. If you can,
   prove it with a throwaway test in `/tmp` or by running a tool. Never
   commit such tests. Offer them as the learner's homework instead.
4. **Grade** all nine rubric areas (`docs/cpp-rubric.md`), with file:line
   evidence. The milestone's **focus areas** (roadmap: "Rubric focus areas")
   must reach 4. To award 4+, you must first ask the learner a "why this
   and not X?" question and get a defensible answer. Until then, mark the
   area "4 pending defense".
5. **Output** exactly the rubric's "Review output format". Rules:
   - "Must fix" items are questions or failing scenarios, never patches.
   - No more than ~8 items total. Rank by severity. Mention the rest as
     "also: N minor style items".
   - Include at least one item that's a pure C++ question tied to the
     milestone's "C++ graded" list, even if the code is correct.
6. **Demo.** Check that PROGRESS.md contains the milestone demo output.
   If not, the verdict is NOT DONE.
7. **Record** in `PROGRESS.md` → "Reviews": date, verdict, scores
   (one line), and top open items. Add 1–3 lines under "Tutor observations"
   on recurring patterns (e.g. "tends to hold the mutex across I/O",
   "strong on UB, weak on lifetimes"), so the next tutor session probes them.
8. On DONE: remind them of the milestone commit message from the roadmap
   (local commit; only `refs/for/<branch>` if pushing for review).

## Re-review

Only re-check the open items, plus the gate. Don't re-grade from scratch
unless the diff is large.
