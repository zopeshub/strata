#pragma once

// The storage abstraction every byte in strata goes through.
//
// Why an interface instead of calling POSIX directly?
//   1. Fault injection: tests/support/mem_fs.h implements this interface in
//      memory and can simulate power loss ("unsynced data vanishes"), which is
//      how the crash-consistency tests work. You cannot test crash recovery
//      properly without this seam.
//   2. Portability: macOS and Linux differ exactly where it matters
//      (F_FULLFSYNC vs fdatasync, O_DIRECT vs F_NOCACHE).
//
// All paths are std::filesystem::path. Durability semantics are POSIX-strict:
// data is durable only after WritableFile::Sync(); a *directory entry*
// (create, rename, unlink) is durable only after SyncDir() on its parent.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "strata/options.h"
#include "strata/status.h"

namespace strata {

using Path = std::filesystem::path;

// Sequential reads, e.g. replaying a WAL. Not thread-safe.
class SequentialFile {
 public:
  virtual ~SequentialFile() = default;
  // Reads up to scratch.size() bytes. The result may point into `scratch`.
  // An empty result means end of file.
  virtual Result<std::string_view> Read(std::span<char> scratch) = 0;
  virtual Status Skip(std::uint64_t n) = 0;
};

// Positional reads, e.g. SSTable blocks. MUST be safe to call concurrently
// from multiple threads (pread(2), not lseek+read).
class RandomAccessFile {
 public:
  virtual ~RandomAccessFile() = default;
  // Reads up to scratch.size() bytes at `offset`. Short only at EOF.
  // The result points either into `scratch` or into memory owned by this
  // file (e.g. an mmap'd region), valid until this object is destroyed. This
  // is why the API returns a view instead of filling a buffer: it lets an
  // mmap implementation be zero-copy (curriculum/03-virtual-memory.md).
  virtual Result<std::string_view> Read(std::uint64_t offset, std::span<char> scratch) const = 0;
  [[nodiscard]] virtual std::uint64_t Size() const = 0;
};

// Append-only writes, e.g. WAL, SSTables, MANIFEST. Not thread-safe.
class WritableFile {
 public:
  // Closes the file if Close() was not called; errors are swallowed, which is
  // why callers on the durability path must call Close() explicitly.
  virtual ~WritableFile() = default;
  virtual Status Append(std::string_view data) = 0;
  // Hands buffered bytes to the kernel (write(2)). NOT durable.
  virtual Status Flush() = 0;
  // Flush(), then make the file's data durable according to the SyncMethod.
  virtual Status Sync() = 0;
  virtual Status Close() = 0;
  // Logical size: bytes appended so far, including any still buffered.
  [[nodiscard]] virtual std::uint64_t Size() const = 0;
};

// An exclusive advisory lock, held for the lifetime of the object.
class FileLock {
 public:
  virtual ~FileLock() = default;
};

class FileSystem {
 public:
  virtual ~FileSystem() = default;

  // Creates or truncates.
  virtual Result<std::unique_ptr<WritableFile>> NewWritableFile(const Path& path) = 0;
  // Creates or opens for append; Size() starts at the current file size.
  virtual Result<std::unique_ptr<WritableFile>> NewAppendableFile(const Path& path) = 0;
  // NotFound if the file does not exist.
  virtual Result<std::unique_ptr<SequentialFile>> NewSequentialFile(const Path& path) = 0;
  virtual Result<std::unique_ptr<RandomAccessFile>> NewRandomAccessFile(const Path& path) = 0;

  [[nodiscard]] virtual bool FileExists(const Path& path) = 0;
  // Entry names (not full paths), excluding "." and "..", in no particular order.
  virtual Result<std::vector<std::string>> GetChildren(const Path& dir) = 0;
  virtual Result<std::uint64_t> GetFileSize(const Path& path) = 0;
  virtual Status RemoveFile(const Path& path) = 0;
  // Atomically replaces `to` if it exists (rename(2) semantics).
  virtual Status RenameFile(const Path& from, const Path& to) = 0;
  // OK if the directory already exists.
  virtual Status CreateDir(const Path& dir) = 0;
  virtual Status RemoveDir(const Path& dir) = 0;
  // Makes directory entries (creates, renames, unlinks) in `dir` durable.
  virtual Status SyncDir(const Path& dir) = 0;
  // Non-blocking exclusive lock. Returns Busy if the lock is held by anyone,
  // *including this process* (think hard about flock vs fcntl locks here).
  virtual Result<std::unique_ptr<FileLock>> LockFile(const Path& path) = 0;
};

struct PosixFileSystemOptions {
  SyncMethod sync_method = kDefaultSyncMethod;
  // Serve RandomAccessFile reads from an mmap of the whole file.
  bool use_mmap_reads = false;
  // User-space buffer size for WritableFile (bytes accumulated before write(2)).
  std::size_t writable_buffer_size = 64u << 10;
};

std::unique_ptr<FileSystem> NewPosixFileSystem(const PosixFileSystemOptions& options = {});

// Process-wide POSIX file system with default options. Never destroyed.
FileSystem* DefaultFileSystem();

}  // namespace strata
