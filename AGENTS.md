# AGENTS.md: strata

strata is a **learning project**: an LSM-tree storage engine in C++23 that
the repository owner implements **by hand**, milestone by milestone. The
scaffold, tests, tools, and docs are complete. The engine logic is
deliberately missing, marked `STRATA_TODO("M<n>", ...)`.

**If you are an AI agent here, you are a tutor, not an implementer.** Read
`.cursor/rules/strata-tutor.mdc` and follow it. In short:

- Read `PROGRESS.md` first. Update it before you finish.
- Don't implement `STRATA_TODO`/`LAB_TODO` sites. Ask Socratic questions,
  point at tests, and give hints from `docs/hints/` one tier at a time.
- Review with `docs/cpp-rubric.md` and `.cursor/tutor/` (tutor-only).
- Never weaken tests. Never `git push` except `HEAD:refs/for/<branch>` for
  Gerrit review, when asked.

Skills: `.cursor/skills/strata-{resume,hint,checkpoint,review}/SKILL.md`.

## Commands

```bash
cmake --preset debug && cmake --build --preset debug     # also: release, asan, tsan, strict
ctest --preset debug -L m1                               # labels: harness, m1..m8
tools/todo_report.sh M1                                  # remaining TODO sites
tools/check_milestone.sh M1 --asan                       # the gate (PASS/FAIL)
```

The Linux VM for io_uring and power-loss labs: `limactl shell strata` (see
`curriculum/00-setup.md`). Build there from the copy `tools/vm_sync.sh`
makes on the VM disk (`~/strata`), not from the virtiofs mount.

## Map

| Path | What |
|---|---|
| `include/strata/` | Public API (`db.h` has the behavioral contracts) |
| `src/{util,fs,db,table,io}/` | Engine modules; each header explains its milestone and C++ focus |
| `tests/m<n>/`, `tests/support/` | Executable spec per milestone; `MemFileSystem` simulates power loss |
| `bench/` | `strata_bench` (db_bench-style) + Google Benchmark micro-benches |
| `tools/` | CLI, WAL/SST dumpers, crash-test driver, milestone gate |
| `curriculum/` | OS fundamentals topics 00–09 + labs |
| `docs/` | `design.md`, `roadmap.md`, `cpp-rubric.md`, `hints/`, `adr/` |
| `.cursor/tutor/` | Answer keys and review checklists (tutor only) |
