# 00: Setup (macOS host + Linux VM)

**Time:** ~45 min (mostly waiting on the VM's first boot). **Feeds:** M0.

> **On a Linux machine?** Skip Lima entirely. See
> [`docs/framework.md` → Running on native Linux](../docs/framework.md#11-running-on-native-linux)
> for the packages to install. Read the concept section below anyway: it's
> the "why" behind the macOS comparisons you'll have to take on faith.

## Concept: why two operating systems?

The engine is portable POSIX C++ and develops fine on macOS. Several storage
experiments are **only honest on Linux**:

- `io_uring` exists only on Linux (M8, topic 06).
- `O_DIRECT`, `/proc/sys/vm/drop_caches`, `fdatasync` that differs from
  `fsync`, `blktrace`/`bpftrace`/`perf` visibility into the block layer.
- **dm-flakey**: a device-mapper target that silently drops writes, so you
  can simulate power loss without pulling a plug (topic 02).

Meanwhile, macOS teaches its own lessons: plain `fsync` there doesn't
flush the drive's write cache (`F_FULLFSYNC` does), Apple Silicon uses
**16 KiB pages** (Linux on the same machine uses 4 KiB), and APFS is
copy-on-write. Code that is correct on both is more likely to be correct.

The VM runs on Apple's Virtualization.framework (`vmType: vz`). Its disk is
a file on your Mac's SSD, so the VM's `fsync` passes through a
virtualization layer before reaching real hardware. That's fine for
learning *semantics* (which syscalls matter, in which order); treat its
*latency* numbers with suspicion (topic 07 asks you why).

## Exercise: build everywhere

On macOS:

```bash
xcode-select --install                       # Apple clang, if not already present
brew install cmake ninja llvm fio jq ripgrep lima
cd ~/Documents/cursor_workspace/strata
cmake --preset debug && cmake --build --preset debug
ctest --preset debug -L harness              # must be green
./build/debug/curriculum/labs/lab05_io_path_probe sync
getconf PAGESIZE                             # note it down
```

`llvm` is keg-only: `clang-tidy` and `clang-format` live in
`$(brew --prefix llvm)/bin`. `tools/check_milestone.sh --tidy` finds it
there. Add it to your `PATH` if you want it in the editor.

The Linux VM (config in `lima/strata.yaml`):

```bash
limactl start --name=strata lima/strata.yaml   # first boot installs gcc-14, liburing, fio, bpftrace...
limactl shell strata tools/vm_sync.sh          # from the repo dir on the Mac: copy to ~/strata in the VM
limactl shell strata
# inside the VM:
cd ~/strata
cmake --preset debug && cmake --build --preset debug
ctest --preset debug -L harness
grep STRATA_HAVE_IO_URING build/debug/CMakeCache.txt   # must say ON
getconf PAGESIZE
mkdir -p ~/lab && export LAB_DIR=~/lab                 # labs write here, on the VM disk
```

Lima mounts your Mac workspace inside the VM at the **same absolute path**
(`/Users/<you>/Documents/cursor_workspace`). `tools/vm_sync.sh` finds it
and rsyncs to the VM's own disk. Workflow: edit on the Mac, then run
`~/strata/tools/vm_sync.sh` in the VM (or `limactl shell strata
tools/vm_sync.sh` from the Mac repo dir) before building there. Don't build
inside the mounted workspace from the VM: the mount is slow, and your macOS
and Linux `build/` directories would collide.
`limactl stop strata` when you're done frees 6 GiB of RAM.

## Checkpoint question

The VM's ext4 file system lives on a virtual disk, which is a file on APFS,
which lives on an SSD with a volatile write cache. The VM's `fsync` returns
success. Walk the layers from the guest's `fsync` to NAND: at each layer,
what would have to happen for the data to really be durable, and which
layer's behavior can you *not* determine from inside the guest?
(You'll measure a piece of this in topic 07.)

## Engine tie-in

`SyncMethod` (`include/strata/options.h`) exists because of this topic:
`kDefaultSyncMethod` is `kFullFsync` on macOS and `kFdatasync` on Linux.
`tools/check_milestone.sh` runs identically on both. Run it in the VM for
at least M1, M7, and M8.

## Going deeper

- Lima docs, "VM types" and "mounts" (why virtiofs, what `vz` means).
- `man fcntl` on macOS, the `F_FULLFSYNC` entry.
