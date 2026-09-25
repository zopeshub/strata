#pragma once

// Move-only RAII owner of a POSIX file descriptor. (M1)
//
// This is the first "real" C++ lesson in the project. Grading focus:
//   * Rule of five done deliberately: which members are defaulted, deleted,
//     or written by hand, and why each move is noexcept.
//   * Self-move-assignment must be safe (fd = std::move(fd) must not close).
//   * Moved-from state is valid and empty (fd_ == -1).
//   * close(2) and EINTR: on Linux the fd is released even if close returns
//     EINTR, so retrying close is a BUG (it may close someone else's fd).
//     What does macOS do? Write down your answer in the code comment.
//   * The destructor cannot report errors. Close() exists for callers that
//     must know (e.g. after writing an SSTable). When does that matter?

#include <string_view>

#include "strata/options.h"
#include "strata/status.h"

namespace strata {

class FileDescriptor {
 public:
  constexpr FileDescriptor() noexcept = default;
  explicit constexpr FileDescriptor(int fd) noexcept : fd_(fd) {}
  ~FileDescriptor();

  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;
  FileDescriptor(FileDescriptor&& other) noexcept;
  FileDescriptor& operator=(FileDescriptor&& other) noexcept;

  [[nodiscard]] int get() const noexcept { return fd_; }
  [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
  explicit operator bool() const noexcept { return valid(); }

  // Gives up ownership without closing; returns the fd (or -1).
  [[nodiscard]] int release() noexcept;
  // Closes the current fd (if any) and takes ownership of `fd`.
  void reset(int fd = -1) noexcept;
  // Closes now and reports failure. Leaves *this empty either way.
  Status Close();

 private:
  int fd_ = -1;
};

// Makes `fd`'s data durable using `method` (see include/strata/options.h and
// curriculum/07-fsync-semantics.md). `context` goes into the error message.
Status SyncFd(int fd, SyncMethod method, std::string_view context);

}  // namespace strata
