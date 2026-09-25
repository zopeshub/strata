# 08: The write-ahead log pattern

**Time:** ~1.5 h. **Runs on:** both. **Feeds:** M2, M7.

## Concept

**The rule.** Before a change is applied to any structure that could be
persisted (or acknowledged), a record describing it is appended to a
log, and before a change is **acknowledged** as durable, its log record is
durable. Recovery replays the log over the last durable state.

**Redo-only in an LSM.** strata's memtable is volatile and immutable
entries are never updated in place, so the WAL only needs **redo**
records: "apply this batch with sequence number S". Recovery = replay
every live WAL into a fresh memtable. Update-in-place engines (InnoDB,
Postgres heap) also need undo information or full-page images, because a
partially flushed page must be repaired. That's ARIES territory, which an
LSM avoids by never overwriting.

**Log anatomy** (LevelDB lineage, strata's format in `docs/design.md` §3):

- Fixed-size **blocks** (32 KiB) so a reader can resynchronize after
  corruption by skipping to the next block boundary.
- Records split into FULL / FIRST / MIDDLE / LAST **fragments** across blocks.
- Every fragment has a **CRC32C** over type + payload, plus a length.
- A torn **tail** (the crash happened mid-append) is expected and silent.
  A bad record *followed by valid data* is corruption.

**Log lifetime.** A WAL can be deleted once every record in it is in a
durable SSTable **and** the metadata saying so is durable (M6:
`log_number` in the MANIFEST). Until M6, strata keeps old logs around.

**Group commit.** An fsync costs the same for 1 record or 100 (topic 05).
So when many writers wait, one **leader** appends all queued batches as
one record, fsyncs once, and wakes the followers. Throughput scales with
concurrency, and latency per writer stays about one fsync. It's the most
important write-path optimization in every database.

**The prefix property.** Records are appended in sequence order, and fsync
covers everything written before it. So the recovered state is always
**all writes up to some point, and nothing after** (never with a hole),
and that point is at or after the last acknowledged sync write. Your crash
tests check exactly this.

## Exercise: lab 08

`curriculum/labs/lab08_group_commit.cc`: naive (lock, append, fsync, unlock
per commit) vs group (leader/follower). Implement `CommitGroup`, predict the
throughput curve for 1–32 threads, then:

```bash
./build/release/curriculum/labs/lab08_group_commit sweep
```

Explain the shape of both curves. Where does group mode stop scaling? What
is the average group size at 32 threads, and what does that imply about p99
latency for a single commit?

## Checkpoint question

Thread A does `Put(k1)` with `sync=false`. After A's call returns, thread B
does `Put(k2)` with `sync=true`, and it returns OK. Then the machine loses
power. Can the recovered DB contain k2 but not k1? Answer for strata's
single-WAL design, and say which property of the log and of `fsync` your
answer depends on. Then: would the answer survive if strata wrote the WAL
with two files in parallel (e.g. sharded per thread) for throughput?

## Engine tie-in

- **M2:** `log::Writer` / `log::Reader` (the format above), `DB::Write`
  (append → optional Sync → apply to memtable → return), and recovery by
  replay. `tools/crash_test.sh` kills the process with `kill -9` in a loop
  and verifies.
- **M6:** `log_number` in the MANIFEST decides which WALs are obsolete.
- **M7:** group commit in `DB::Write`, measured by `strata.wal-syncs` in
  `tests/m7/group_commit_test.cc` and by `strata_bench --benchmarks=fillsync --threads=N`.

## Going deeper

- LevelDB `doc/log_format.md`, the design strata's format follows.
- Mohan et al., *ARIES* (TODS 1992), sections 1–3, to see what an LSM gets to skip.
