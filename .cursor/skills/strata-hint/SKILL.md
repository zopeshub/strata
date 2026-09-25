---
name: strata-hint
description: Gives the strata learner exactly one hint tier for the problem they are stuck on, using docs/hints/m<n>.md, and logs it. Use when the learner says /strata-hint, asks for a hint, or is stuck on a strata milestone.
disable-model-invocation: true
---

# Give one hint

## Steps

1. Identify the milestone and the specific problem. If unclear, ask one
   question: "Which function, and what did you try?"
2. Ask what they've tried if they haven't said. The 20-minute rule is theirs
   to honor. Don't police it, but do ask for their current hypothesis.
3. Look at their code for that function (read the file). Often the best
   hint is a question about *their* code, not the generic ladder: "What
   does line 88 return when `input` has 9 bytes?" Prefer that, if it exists.
4. Otherwise, open `docs/hints/m<n>.md`, find the matching section, and see
   in `PROGRESS.md` ("Hints used") which tier they already took for it.
   Give **the next tier only**, paraphrased to fit their situation. Never
   skip a tier. Never go past tier 3.
5. If tier 3 is exhausted and they're still stuck, don't give code.
   Instead, pick one:
   - Write a *new failing test case* description (inputs + expected output)
     that isolates the bug. They write the test.
   - Point at the corresponding function in LevelDB's source by name (e.g.
     `leveldb::log::Reader::ReadPhysicalRecord`) as reading material. They
     must explain it back before using it.
6. Append to `PROGRESS.md` → "Hints used": `M<n> <section> tier <k> (date)`.
