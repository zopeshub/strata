# 05: I/O paths, from write(2) to the device

**Time:** ~1.5 h. **Runs on:** both. Tracing is richest in the Linux VM.
**Feeds:** M5, M8.

## Concept

A buffered `write(2)` on Linux, top to bottom:

1. **Syscall → VFS:** `vfs_write` finds the file's `write_iter` method.
2. **File system:** copies user bytes into page-cache pages (allocating
   them, maybe reading the partial page first), marks them dirty, updates
   size/mtime in the in-memory inode. **Returns.** No I/O has happened.
3. **Later (writeback, or `fsync`):** the FS maps dirty pages to disk
   blocks (delayed allocation happens *now*) and builds `bio`s.
4. **Block layer (blk-mq):** merges adjacent bios into requests, runs an
   I/O scheduler (`none` for NVMe, maybe `mq-deadline`), and dispatches onto
   per-CPU hardware queues.
5. **Driver → device:** NVMe has up to 64k submission/completion queue
   pairs in host memory. The device DMAs the data and posts a completion,
   and an interrupt (or polling) completes the request.
6. **`fsync` extra:** after data I/O completes, ext4 commits its journal
   (another write, often with **FUA**), and the kernel sends a **FLUSH**
   so the device's volatile write cache is emptied. One logical fsync
   is several device operations.

**Interfaces for asynchronous I/O**, in historical order: blocking calls
in thread pools; POSIX AIO (in glibc, *also* a thread pool); Linux `libaio`
(truly async only with `O_DIRECT`, still blocks on metadata); `io_uring`
(topic 06). macOS offers POSIX AIO (kqueue-integrated) and
`dispatch_io`, and its I/O stack (APFS, IOKit) is mostly opaque. `fs_usage`
is your window into it, since dtrace is restricted by SIP.

**Latency scale** (orders of magnitude): syscall ~100 ns; page-cache
4 KiB read ~1 µs; NVMe 4 KiB read ~50–100 µs; NVMe FLUSH on consumer
drives ~0.5–5 ms; in a VM, add the host's path on top.

## Exercise: lab 05

`curriculum/labs/lab05_io_path_probe.cc` is complete. The lab is observing it.

In the VM (with `LAB_DIR=~/lab`), run each mode under each tool:

```bash
P=./build/debug/curriculum/labs/lab05_io_path_probe
strace -f -T -e trace=openat,write,fsync,fdatasync $P sync
sudo bpftrace curriculum/labs/lab05_block_latency.bt &   # then run $P buffered / sync / odsync, then fg + Ctrl-C
```

On macOS: `sudo fs_usage -w -f filesys lab05_io_path_probe` in one
terminal, and run the probe in another.

Answer: in `buffered` mode, when do block I/Os actually get issued? How
many device operations does one `fsync` cause in `sync` mode, and what are
their `rwbs` flags? What does `O_DSYNC` change?

## Checkpoint question

Your bpftrace output shows that one 4 KiB append + `fsync` on ext4 costs
more than one block write. List the device operations you saw (or expect),
in order, with flags, and say which exist only because the file's *size*
changed. Then design around it: which two WAL techniques eliminate most of
the metadata traffic from a synced append, and what does each cost?

## Engine tie-in

- **M5:** flush and compaction are large sequential writes. Buffered I/O
  plus one `fsync` at the end is right for them. The WAL is small writes
  that must be durable one by one: different path, different costs.
- **M7:** group commit exists because step 6 is expensive and the same
  for 1 record or 100.
- **M8:** the `IoContext` blocking backend is "step 1–6 inside `pread`
  on the calling thread". The io_uring backend submits many of them and
  reaps completions. Knowing where each one blocks tells you what "async"
  can actually overlap.

## Going deeper

- Brendan Gregg, *BPF Performance Tools*, ch. 9 (Disk I/O).
- Linux `Documentation/block/blk-mq.rst`.
