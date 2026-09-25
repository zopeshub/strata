#pragma once

// Asynchronous positional reads for coroutines. (M8)
//
// Two backends behind one interface:
//   * Blocking: pread() inline, resume immediately. Works everywhere
//     (macOS included). The baseline.
//   * io_uring (Linux only): SubmitRead() queues an SQE whose user_data is
//     the ReadOp*; RunUntilComplete() submits, reaps CQEs, and resumes the
//     coroutine that owns each op. Many reads in flight from ONE thread.
//
//   Task<Result<std::size_t>> ReadAll(IoContext& io, int fd, ...) {
//     auto n = co_await io.Read(fd, offset, buffer);   // suspends here
//     ...
//   }
//
// Lifetime puzzle (the reviewer WILL ask): the ReadOp lives inside the
// awaiter, which lives inside the suspended coroutine's frame. Why is it
// safe for the kernel to write the result into it later? What happens if
// the Task is destroyed while the read is in flight? (Answer that in an ADR,
// and make it impossible or safe.)

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "io/task.h"
#include "strata/status.h"

namespace strata {

struct ReadOp {
  int fd = -1;
  std::uint64_t offset = 0;
  std::span<char> buffer;
  // Filled in on completion: bytes read (>= 0) or -errno.
  long result = 0;
  std::coroutine_handle<> waiter;
};

class IoContext {
 public:
  virtual ~IoContext() = default;

  // Awaitable positional read. Result<size_t>: bytes read (short only at EOF).
  class ReadAwaiter {
   public:
    ReadAwaiter(IoContext& ctx, int fd, std::uint64_t offset, std::span<char> buffer)
        : ctx_(ctx), op_{fd, offset, buffer, 0, {}} {}
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) {
      op_.waiter = h;
      ctx_.SubmitRead(op_);
    }
    Result<std::size_t> await_resume() const {
      if (op_.result < 0)
        return Err(Status::FromErrno("async read", static_cast<int>(-op_.result)));
      return static_cast<std::size_t>(op_.result);
    }

   private:
    IoContext& ctx_;
    ReadOp op_;
  };

  ReadAwaiter Read(int fd, std::uint64_t offset, std::span<char> buffer) {
    return ReadAwaiter(*this, fd, offset, buffer);
  }

  // Start the op. Must eventually set op.result and resume op.waiter, from
  // inside RunUntilComplete (never from another thread).
  virtual void SubmitRead(ReadOp& op) = 0;

  // Runs the event loop until no ops are in flight. Returns false if the
  // backend failed irrecoverably.
  virtual bool RunUntilIdle() = 0;

  // Starts `task` and drives the loop until it completes; returns its value.
  template <class T>
  T RunUntilComplete(Task<T> task);
};

std::unique_ptr<IoContext> NewBlockingIoContext();

#if defined(STRATA_HAVE_IO_URING)
// queue_depth: SQ entries. Returns an error if io_uring_queue_init fails
// (e.g. inside some containers io_uring is disabled by seccomp).
Result<std::unique_ptr<IoContext>> NewIoUringContext(unsigned queue_depth);
#endif

template <class T>
T IoContext::RunUntilComplete(Task<T> /*task*/) {
  // TODO(M8): resume the task's handle, then loop RunUntilIdle() until the
  // task is done; then extract its result (same mechanism as SyncWait).
  STRATA_TODO("M8", "IoContext::RunUntilComplete");
}

}  // namespace strata
