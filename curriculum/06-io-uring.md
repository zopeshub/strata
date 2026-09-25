# 06: io_uring

**Time:** ~2 h. **Runs on:** Linux VM only. **Feeds:** M8.

## Concept

**Two rings in shared memory.** The application writes **submission
queue entries** (SQEs: opcode, fd, buffer, offset, `user_data`) into the
SQ ring and bumps the tail. The kernel consumes them, performs the
operations, and writes **completion queue entries** (CQEs: `user_data`,
`res`) into the CQ ring. Because the rings are shared (`mmap`ed), one
`io_uring_enter` syscall can submit many SQEs *and* wait for completions.
With `IORING_SETUP_SQPOLL`, a kernel thread polls the SQ and submission
needs no syscall at all.

**`user_data` is the whole programming model.** It's 64 opaque bits
returned untouched in the CQE, typically a pointer to your per-operation
state. Completions arrive **in any order**. Everything about correctness
is lifetime management of what `user_data` points to, and of the buffer the
kernel will write into.

**What's actually asynchronous?** For `O_DIRECT` reads on a block device,
the request goes straight to the driver: truly async. For **buffered**
reads that miss the page cache, the kernel first tries a non-blocking
attempt. If that would block, it **punts** the request to an internal
worker pool (`io-wq`) that does a blocking read. That's still useful (no
thread management on your side), but it's a thread pool, just the
kernel's. Newer kernels handle more cases natively (async buffered reads
since 5.9).

**Features you'll see mentioned:** registered files and buffers (skip
per-op fd lookup and page pinning), linked SQEs (`IOSQE_IO_LINK`: a write
then an fsync, in order), `IORING_OP_FSYNC`, multishot operations,
`IORING_OP_ASYNC_CANCEL`. Cancelling an op doesn't mean it's done: you
still get a CQE, and until then the kernel may touch your buffer.

**Caveat:** io_uring's attack surface led several environments (some
container runtimes' seccomp profiles, ChromeOS, Android, some cloud
kernels) to disable it. Production code keeps a fallback path, as strata
does.

## Exercise: lab 06 (Lima VM)

`curriculum/labs/lab06_uring_read.cc`. Implement `ReadAll`: keep up to
`depth` 4 KiB reads in flight, reuse slots as completions arrive, and
handle short reads and `-errno`. The harness verifies every byte against
`pread`.

```bash
L=./build/debug/curriculum/labs/lab06_uring_read
dd if=/dev/urandom of=~/lab/u.dat bs=1M count=2048
for d in 1 4 16 64; do sync; echo 1 | sudo tee /proc/sys/vm/drop_caches >/dev/null; $L ~/lab/u.dat $d; done
```

Plot throughput vs depth. Where does it stop scaling, and why there? Then
check how many `iou-wrk` threads appear during a cold buffered run
(`ps -eLf | grep iou`). What does that tell you?

## Checkpoint question

In M8, `co_await io.Read(fd, off, buf)` creates an awaiter whose `ReadOp`
(including `buf` and the `result` field the completion writes) lives
**inside the suspended coroutine's frame**. The `Task` owning that frame is
destroyed while the SQE is still in flight. Walk through exactly what happens
next, down to the CQE and the kernel's write into `buf`. Then give two
designs that make this impossible or safe, and say which one you'll choose
for strata and why.

## Engine tie-in

- **M8:** `src/io/io_uring_context.cc` is lab 06 wrapped behind
  `IoContext`, with coroutines instead of slot bookkeeping.
  `bench/bench_async_read` compares blocking vs io_uring at various batch
  sizes. Run it with `STRATA_BENCH_DIRECT=1` to see the O_DIRECT difference.
- The lifetime question above is the M8 ADR.

## Going deeper

- Jens Axboe, *Efficient IO with io_uring* (kernel.dk/io_uring.pdf), the design document.
- *Lord of the io_uring* (unixism.net/loti), for liburing idioms.
- `man 7 io_uring`, `man 3 io_uring_prep_read`.
