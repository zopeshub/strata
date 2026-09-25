# 01: Filesystems: names, inodes, and atomic replacement

**Time:** ~1.5 h. **Runs on:** macOS (Linux works too). **Feeds:** M1, M5, M6.

## Concept

**Three layers of naming.** A file descriptor refers to an *open file
description* (offset, flags), which refers to an *inode* (the file: size,
block map or extents, permissions, link count), which is referred to by
zero or more *directory entries* (name → inode number). A directory is
itself a file whose contents are that mapping. The VFS layer (Linux) or
vnode layer (macOS) caches both inodes and entries (the *dentry cache*),
so path lookup rarely touches disk.

**Consequences you'll rely on in the engine:**

- `unlink(name)` removes a directory entry. The inode and its data live
  until the link count *and* the open count reach zero. A reader holding an
  fd keeps reading the old file, bytes intact, after you've "deleted" it.
- `rename(old, new)` is **atomic with respect to other processes**. At every
  instant, `new` names either the old inode or the new one, never nothing,
  never a mix. POSIX guarantees that. It does *not* by itself guarantee
  anything about power loss (topic 02).
- `write(2)` is *not* atomic with respect to concurrent readers of the
  same byte range. `O_TRUNC` followed by `write` opens a window where the
  file is empty or partial. That's why "rewrite the config file in place"
  is a bug even without crashes.
- `O_APPEND` makes "seek to end + write" atomic for the *offset*: two
  appenders never overwrite each other. Content interleaving under
  concurrent large appends is still possible.
- File **locks**: `flock(2)` locks belong to the open file description.
  POSIX `fcntl` locks belong to the (process, inode) pair and are released
  when the process closes *any* fd for that inode. That's a notorious trap
  for libraries that open the same file twice.

**Space management.** Modern file systems allocate in *extents* (start,
length) and delay allocation until writeback (ext4 delalloc, XFS, APFS),
which is why a freshly written file may have no blocks on disk yet. APFS
and btrfs are copy-on-write: overwriting a block writes a new one and
updates pointers up the tree. ext4 and XFS overwrite in place and journal
metadata.

## Exercise: lab 01

`curriculum/labs/lab01_atomic_rename.cc`: a reader process checks, in a
tight loop, that a config file always holds one complete payload while a
writer rewrites it thousands of times.

1. Predict the torn-read count for `naive` mode. Then run
   `./build/debug/curriculum/labs/lab01_atomic_rename naive`.
2. Implement the `atomic` mode's `LAB_TODO` (temp file → write → rename).
   It must report **0 torn reads**.
3. Answer the question in the file's header: which of your syscalls matter
   only for power loss, and not at all for this test? Leave them in, and
   say so in a comment.

## Checkpoint question

In M6, compaction deletes an input SSTable while a long-running iterator
(or a `TableCache` entry) still has it open. Explain, at the level of
directory entries, inodes, and open file descriptions, why the iterator
keeps working. Then explain what goes wrong if the engine instead *reused
file numbers*, and why strata's filenames are monotonically increasing numbers.

## Engine tie-in

- **M1:** `PosixFileSystem::RenameFile`, `LockFile` (`flock`; the test
  `LockSurvivesClosingAnotherFdOfTheSameFile` exists because of the fcntl
  trap), `GetChildren`.
- **M5:** a flush writes `NNNNNN.dbtmp` and renames it to `NNNNNN.sst`, so
  recovery never sees a half-written table under a real name.
- **M6:** `CURRENT` is replaced by write-temp + rename (`SetCurrentFile`).
  Obsolete-file deletion relies on unlink-while-open semantics.

## Going deeper

- OSTEP ch. 39 (*Files and Directories*) and ch. 40 (*File System Implementation*).
- `man 2 rename` (Linux), the paragraph starting "If newpath already exists".
