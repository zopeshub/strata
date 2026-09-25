# 09: SSD internals, and why LSMs fit them

**Time:** ~1.5 h. **Runs on:** both (the VM gives cleaner numbers). **Feeds:** M6, benchmarks.

## Concept

**NAND constraints.** Flash is read and programmed in **pages** (4–16
KiB), but erased only in **erase blocks** (several MiB). A page can't be
overwritten. It must be erased first, and each block survives a limited
number of erase cycles.

**The FTL hides this.** The flash translation layer keeps a logical →
physical page map. Every write goes to a fresh page, and the old page
becomes garbage. **Garbage collection** picks blocks, copies their still-live
pages elsewhere, and erases them. Those copies are **device-level write
amplification**: the host wrote 1 GiB, the NAND absorbed 3 GiB.
Over-provisioning (hidden spare capacity) and **TRIM** (telling the drive
which logical pages are dead) reduce it.

**Parallelism.** An SSD is dozens of dies behind several channels. One
4 KiB read at queue depth 1 uses one die. You only see the drive's rated
IOPS at high queue depth (topic 06's lab showed this from the host side).

**The unwritten contract** (He et al., EuroSys '17): SSDs perform best when
software issues large requests, keeps locality, writes sequentially and
aligned, **groups data by death time** (data written together dies
together, so erase blocks die whole and GC copies nothing), and keeps
lifetimes uniform.

**LSM trees honor that contract almost by construction:** sequential
immutable writes (WAL, SSTables), large aligned files, and whole-file death
(compaction deletes entire SSTables, and TRIM frees them). The price is
**engine-level write amplification**: leveled compaction rewrites each byte
roughly once per level, times ~the level fanout. Total WA = engine WA ×
device WA. The RUM conjecture frames the trade: read, update, and memory
overheads can't all be minimized at once. Leveled compaction favors reads
and space, and tiered favors writes.

## Exercise: lab 09

`curriculum/labs/lab09_fio.sh ~/lab` in the VM (and on macOS, noting the
caveats the script prints). It runs 4 KiB random reads at QD1 and QD32,
1 MiB sequential reads and writes, 4 KiB random writes, and the **WAL
pattern** (4 KiB sequential writes, each fsynced). Before running, predict
the ratio between randread QD1 and QD32 IOPS, and between seqwrite MiB/s and
the WAL pattern's MiB/s. Record everything in `curriculum/NOTES.md` next to
your lab 07 numbers.

## Checkpoint question

After M6, `strata_bench --benchmarks=fillrandom,stats` reports write
amplification. Derive the WA you *expect* from strata's defaults:
`write_buffer_size` 4 MiB, `max_bytes_for_level_base` 10 MiB, multiplier
10, a 10 GiB dataset, counting the WAL. Say which assumptions you made. Then
explain why the number you measure is lower or higher, and why the SSD's own
write amplification is plausibly *low* for this workload, even though the
engine rewrites data many times.

## Engine tie-in

- **M6:** leveled compaction's level sizing, target file size, and the
  "grandparent overlap" output cut all exist to bound write amplification
  and keep compactions small. `strata.compaction-bytes-written` and
  friends expose it.
- **Benchmarks:** `strata_bench` reports WA = (WAL + flush + compaction
  bytes) / user bytes. Your README's results section should show it next to
  throughput, and name the device.

## Going deeper

- He et al., *The Unwritten Contract of Solid State Drives*, EuroSys 2017.
- Athanassoulis et al., *Designing Access Methods: The RUM Conjecture*, EDBT 2016.
- O'Neil et al., *The Log-Structured Merge-Tree*, Acta Informatica 1996, the original.
