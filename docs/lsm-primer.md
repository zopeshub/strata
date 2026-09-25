# LSM trees from first principles

A 30-minute read for before M1. It explains **why** a log-structured merge
tree is shaped the way it is, so the formats in [`design.md`](design.md)
and the milestones in [`roadmap.md`](roadmap.md) feel inevitable rather
than arbitrary. No strata code here: just the ideas, a worked example, and
the vocabulary.

---

## 1. The problem a storage engine solves

A key-value storage engine keeps a **sorted map from byte-string keys to
byte-string values** on durable storage. It supports `Put`, `Delete`,
`Get`, and ordered range scans, and it must survive crashes: once `Put`
returns "durable", that write must be there after a power loss.

There are two big families of designs, and they differ in one decision:
**what happens on disk when a key is updated?**

- **Update in place (B-trees):** find the page holding the key, modify it,
  and write the page back. Reads are cheap (one root-to-leaf path), but
  every update is a **random write**. In the worst case, a 100-byte update
  dirties a whole 4–16 KiB page. Crash safety needs a WAL *and* protection
  against torn pages. Examples: InnoDB, PostgreSQL, SQLite, LMDB.
- **Log-structured (LSM trees):** never modify anything on disk. Buffer
  writes in memory, then write them out as new, sorted, **immutable
  files**, and merge those files in the background. Every write to disk is
  **sequential**. Examples: LevelDB, RocksDB, Pebble (CockroachDB),
  Cassandra, ScyllaDB, HBase, Bigtable.

Why does "sequential" matter so much? On disks, a random write costs a
seek (milliseconds). On SSDs, random small writes cost device-level garbage
collection and wear (topic 09), and sync writes cost a flush each (topic 07).
Turning many small random writes into a few large sequential ones is the
LSM trade: **writes get cheap, and reads and background work get more
expensive**. The whole engine exists to keep that "more expensive" bounded.

## 2. The idea in four moves

1. **Log the write, then buffer it.** Append the update to a **write-ahead
   log** (WAL: one sequential file), then insert it into an in-memory
   sorted structure, the **memtable**. The WAL makes it durable. The memtable
   makes it readable. Acknowledge the write.
2. **Flush when full.** When the memtable reaches a size limit (4 MiB by
   default in strata), freeze it (it becomes the **immutable memtable**),
   start a fresh one, and write the frozen one to disk as a sorted file: an
   **SSTable** (sorted string table). Now its WAL can be deleted.
3. **Read newest to oldest.** A key's current value is in the newest place
   that mentions it: the memtable, then the immutable memtable, then
   SSTables from newest to oldest.
4. **Merge in the background.** Many small files make reads slow, and keep
   dead data (overwritten values, deleted keys) around forever. **Compaction**
   merges sorted files into fewer, larger sorted files, dropping what's dead.

Everything else (bloom filters, levels, the MANIFEST, group commit,
snapshots) is an optimization or a safety mechanism around those four moves.

## 3. Updates and deletes without modifying anything

If nothing is modified in place, how do you overwrite or delete?

- **Overwrite** = write a *newer* entry for the same key. Every write gets
  a monotonically increasing **sequence number**, and the newest sequence
  number wins.
- **Delete** = write a **tombstone**: an entry that says "this key is
  deleted as of sequence N". It shadows older values until compaction can
  drop both.

So internally, each entry is `(user key, sequence number, type)`, where
the type is value or deletion. This is the **internal key**. Entries are
sorted by user key ascending, then by sequence number **descending**, so
the newest version of a key comes first.

### Worked example

```
seq 1  Put("apple",  "red")
seq 2  Put("banana", "yellow")
seq 3  Delete("apple")
seq 4  Put("cherry", "dark")
seq 5  Put("apple",  "green")
```

The memtable, in internal-key order:

```
("apple",  5, value)    -> "green"     <- newest apple: Get("apple") = "green"
("apple",  3, deletion)
("apple",  1, value)    -> "red"
("banana", 2, value)    -> "yellow"
("cherry", 4, value)    -> "dark"
```

- `Get("apple")` finds the first entry for "apple": seq 5, "green".
- A **snapshot** taken after seq 3 reads "as of sequence 3". It skips
  entries with a higher sequence, finds `("apple", 3, deletion)`, and
  answers "not found". Snapshots are nearly free: a snapshot is just a
  number, plus a promise not to garbage-collect the versions it can see.
- A scan hides tombstones and yields each key once: apple=green,
  banana=yellow, cherry=dark.

Compaction may drop `("apple", 1)` and `("apple", 3)` **only if no live
snapshot can still see them**, and it may drop a tombstone only when no
older version of the key could exist further down the tree. Otherwise the
deleted value would "resurrect". Getting these rules exactly right is M6.

## 4. What's on disk

| File | What it is | Written | Milestone |
|---|---|---|---|
| `000007.log` | WAL: every batch appended, checksummed, in 32 KiB blocks | continuously, fsynced per write or per group | M2 |
| `000012.sst` | SSTable: sorted data blocks, an index, a bloom filter, a footer | once, at flush or compaction, then never modified | M4 |
| `MANIFEST-000005` | log of changes to the set of live files ("add file 12 at L0", "delete 9, 10") | appended on every flush or compaction | M6 |
| `CURRENT` | the name of the current MANIFEST | replaced atomically (temp file + rename) | M6 |
| `LOCK` | prevents two processes opening the same DB | at open | M2 |

An **SSTable** is built for reads. Keys are packed into ~4 KiB **data
blocks**, with prefix compression. An **index block** holds one entry per
data block ("keys up to X are in the block at offset Y"). A **bloom filter**
answers "is this key possibly in this file?" in memory. A lookup in one
SSTable costs, at most, one data-block read. Often it costs none, because
the bloom filter says "definitely not here". Details: design.md §4.

## 5. Levels and compaction

With flushes alone, the number of SSTables grows forever, and a `Get` for a
missing key would have to check every one. **Leveled compaction** (LevelDB's
design, strata's too) organizes files into levels:

```
L0   [a..z] [c..q] [b..y]            flushed memtables; ranges OVERLAP; newest first
L1   [a..f] [g..m] [n..z]            10 MiB total; ranges DISJOINT
L2   [a..b] [c..d] ... [y..z]        100 MiB; disjoint
L3   ...                             1 GiB; disjoint (each level ~10x the previous)
```

- **L0** files overlap (each is a whole memtable), so a Get may check all of them.
- **L1 and below** hold non-overlapping key ranges, so a Get checks **at most
  one file per level** (binary search on file ranges).
- When L0 has too many files (4), or a level exceeds its size budget,
  compaction picks a file (or all overlapping L0 files), finds the
  overlapping files in the next level, merge-sorts them, drops dead
  entries, and writes new files into the next level. Then it atomically
  swaps the old files for the new ones via a MANIFEST edit.

The alternative, **tiered (size-tiered/universal) compaction**, merges
similarly-sized runs without enforcing disjoint levels. It writes less,
but reads and space overhead grow. It's Cassandra's traditional default,
and RocksDB offers both. strata does leveled compaction, and tiered is a stretch goal.

## 6. The three amplifications

Every LSM design decision trades between three costs:

| | Definition | Leveled (strata) | Tiered |
|---|---|---|---|
| **Write amplification** | bytes written to disk / bytes the user wrote | high: ~10 per level (the fanout) + WAL + flush, often 10–30 in practice | lower: about the number of levels |
| **Read amplification** | disk reads per lookup | low: ≤ 1 file per level, and bloom filters skip most | higher: many overlapping runs |
| **Space amplification** | disk bytes / live data bytes | low: ~1.1 (90% of data sits in the last level) | high: up to ~2× during merges |

The **RUM conjecture** (read, update, memory) says you can't minimize all
three at once. Leveled compaction pays write amplification to buy cheap
reads and space. You'll measure all three with `strata_bench` (M5, M6), and
topic 09 explains why the SSD's own write amplification stays low
underneath.

## 7. Crash recovery

After a crash, **nothing in memory survived**, but everything needed is on disk:

1. Read `CURRENT`, which names the MANIFEST. Replay the MANIFEST to learn
   which SSTables are live at which level, and which WALs are still needed.
2. Replay those WALs, in order, into a fresh memtable. A half-written record
   at the end of a WAL (the crash hit mid-append) is detected by its
   checksum and treated as the end of the log.
3. Delete files that nothing references (leftovers from a flush or
   compaction interrupted by the crash).

The guarantee this buys is the **prefix property**: the recovered database
reflects *all* writes up to some point, and none after it, never with a
gap. That point is at or after the last write acknowledged as durable. Your
crash tests (M2, M6) check exactly this. Making it true requires fsyncing
the right files **and directories** in the right order: topics 02, 07, and 08.

## 8. Concurrency, briefly

Immutability makes concurrency tractable:

- SSTables never change, so any number of readers can use them without locks.
- The set of live files is an immutable **Version**. Compaction builds a new
  Version and swaps a pointer. Readers holding the old one keep it alive (and
  its files undeleted) until they're done.
- The memtable is the only mutable shared structure. strata's is a skiplist
  that one writer modifies while readers traverse it **without locks**,
  using careful atomics (M3).
- Many writers funnel into one WAL. **Group commit** lets one fsync cover
  a whole batch of concurrent writers (M7).
- Compaction and flush run on a background thread, and the engine must never
  hold its main lock across disk I/O.

## 9. LSM vs B-tree: when each wins

| Workload | Better fit | Why |
|---|---|---|
| Write-heavy, ingest, time series, logs | LSM | sequential writes, cheap inserts |
| Many point reads of recently written keys | LSM | the memtable is hot |
| Read-heavy with rare updates | B-tree | one path per read, no compaction |
| Predictable latency, no background work | B-tree | compaction causes write stalls and latency spikes |
| Space-constrained storage | leveled LSM | little fragmentation, and compression friendly |
| Large values | neither as-is | WiscKey-style key-value separation |

## 10. Glossary

| Term | Meaning |
|---|---|
| **memtable** | in-memory sorted buffer of recent writes (a skiplist in strata) |
| **immutable memtable** | a full memtable waiting to be flushed |
| **WAL** | write-ahead log, which makes memtable contents durable |
| **SSTable** | an immutable sorted file of key-value entries with an index and filter |
| **L0, L1, …** | levels. L0 files overlap; L1+ files within a level are disjoint |
| **flush** | writing a memtable to a new L0 SSTable |
| **compaction** | merging SSTables into the next level, dropping dead entries |
| **sequence number** | global monotonically increasing write counter; newest wins |
| **internal key** | user key + sequence number + type (value or deletion) |
| **tombstone** | an entry recording a deletion |
| **snapshot** | a read view fixed at a sequence number |
| **Version** | an immutable description of which files are live at which level |
| **VersionEdit** | a delta between Versions ("add file 12 to L1, remove 9") |
| **MANIFEST** | the log of VersionEdits; CURRENT points at it |
| **bloom filter** | a compact probabilistic set with no false negatives, used to skip files |
| **write stall** | throttling writers when compaction falls behind |
| **group commit** | many writers' records made durable with one fsync |

## 11. Further reading, in order of value

1. O'Neil et al., *The Log-Structured Merge-Tree*, Acta Informatica 1996:
   the original idea, heavier on math than you need.
2. LevelDB `doc/impl.md`: four pages, and strata follows it closely.
3. RocksDB wiki: *Leveled Compaction*, *Universal Compaction*, and *Write
   Stalls*: production-grade detail.
4. Luo & Carey, *LSM-based Storage Techniques: A Survey*, VLDB Journal 2020:
   the map of the whole design space.
5. Lu et al., *WiscKey: Separating Keys from Values in SSD-conscious
   Storage*, FAST 2016: what to do when values are large.
