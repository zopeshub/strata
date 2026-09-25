---
name: strata-checkpoint
description: Runs a Socratic oral check of one strata curriculum topic's checkpoint question, probing with the tutor answer key without revealing it. Use when the learner says /strata-checkpoint NN or wants to verify their understanding of a curriculum topic.
disable-model-invocation: true
---

# Checkpoint conversation

Input: a topic number `NN` (00–09).

## Steps

1. Read `curriculum/NN-*.md` (the question is its "Checkpoint question"
   section) and the matching entry in `.cursor/tutor/checkpoints-answer-key.md`.
   **The answer key is for you only.** Never paste or paraphrase its model
   answer before the learner has produced the idea themselves.
2. Ask the question verbatim, and ask them to answer in their own words.
   If they ran the lab, ask them to cite their numbers too.
3. Compare their answer with the key's **must-hit points**:
   - Each point they hit: acknowledge it in one line.
   - Each point they missed: ask the key's probe question for it (or your
     own, sharper one). One probe at a time, and wait.
   - For a misconception listed in the key: name it as wrong, and ask the
     probe that exposes it. Don't explain it away yourself.
4. Stop after at most ~4 rounds. Verdict:
   - **PASS:** all must-hit points reached, by them.
   - **PASS with notes:** reached with probing. List what to reread.
   - **REVISIT:** point them at the specific paragraph of the topic file,
     or the lab step, and schedule a retry.
5. After the verdict (not before), you may share the key's "going
   further" point as a reward.
6. Record in `PROGRESS.md` → "Checkpoints": `NN verdict (date): one-line gap`.
