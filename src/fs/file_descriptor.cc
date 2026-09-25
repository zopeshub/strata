#include "fs/file_descriptor.h"

#include "strata/todo.h"

namespace strata {

FileDescriptor::~FileDescriptor() {
  if (fd_ >= 0) STRATA_TODO_NOEXCEPT("M1", "~FileDescriptor: close the fd");
}

FileDescriptor::FileDescriptor(FileDescriptor&& /*other*/) noexcept {
  STRATA_TODO_NOEXCEPT("M1", "FileDescriptor move constructor");
}

FileDescriptor& FileDescriptor::operator=(FileDescriptor&& /*other*/) noexcept {
  STRATA_TODO_NOEXCEPT("M1", "FileDescriptor move assignment");
}

int FileDescriptor::release() noexcept { STRATA_TODO_NOEXCEPT("M1", "FileDescriptor::release"); }

void FileDescriptor::reset(int /*fd*/) noexcept {
  STRATA_TODO_NOEXCEPT("M1", "FileDescriptor::reset");
}

Status FileDescriptor::Close() { STRATA_TODO("M1", "FileDescriptor::Close"); }

// TODO(M1): the heart of durability. Map each SyncMethod to the right call:
//   macOS:  fsync(2), fcntl(F_FULLFSYNC), fcntl(F_BARRIERFSYNC). macOS has no
//           public fdatasync; fall back to fsync.
//   Linux:  fsync(2), fdatasync(2); F_FULLFSYNC/F_BARRIERFSYNC don't exist.
// Use #if defined(__APPLE__) / __linux__ and make unsupported combinations
// degrade to the strongest available call, never a weaker one.
// Retry on EINTR. Do NOT retry on EIO: after a failed fsync on Linux the
// kernel may have already marked the dirty pages clean ("fsyncgate", 2018),
// so a retry can "succeed" while your data is gone. Return the error; the
// caller must treat the file as poisoned. (curriculum/07-fsync-semantics.md)
Status SyncFd(int /*fd*/, SyncMethod /*method*/, std::string_view /*context*/) {
  STRATA_TODO("M1", "SyncFd");
}

}  // namespace strata
