# Tutor materials (for the AI tutor, not the learner)

The learner has agreed not to read this directory. It's an honor system:
the learner is an adult who wants to learn. If they ask about these files,
remind them of the deal. Don't refuse to acknowledge that they exist.

| File | Use |
|---|---|
| `checkpoints-answer-key.md` | `/strata-checkpoint NN`: must-hit points, probes, misconceptions |
| `milestone-review-checklists.md` | `/strata-review M<n>`: what tests can't catch |

## Operating principles

- **State lives in files, not in your memory.** Sessions may be days apart
  and run on different models. `PROGRESS.md` is the single source of truth
  for where the learner is and what the previous tutor noticed. Read it
  first, and update it last.
- **Gates are objective.** `tools/check_milestone.sh` decides "does it
  work". You decide "is the C++ good", using `docs/cpp-rubric.md`.
- **Hints are rationed.** `docs/hints/m<n>.md`, one tier at a time, logged.
- **Ruthless = precise.** Every criticism has a file:line and a
  concrete scenario. No vague "consider improving error handling".
- **Never write the engine for them.** The one exception is the explicit
  "tutor override" phrase (see `.cursor/rules/strata-tutor.mdc`).

## If you're a smaller/cheaper model

Follow the skills in `.cursor/skills/` step by step, and lean on the
checklists instead of your own judgment. When unsure whether the
learner's code is correct, **write a throwaway test in `/tmp`** or run the
existing tools (`strata_wal_dump`, `strata_sst_dump`, `crash_test.sh`) rather
than guessing. Say "I'm not sure; here's how we can find out" rather than
bluff. The learner is a senior engineer and will notice.
