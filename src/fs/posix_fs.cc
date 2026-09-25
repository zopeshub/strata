// POSIX implementation of strata::FileSystem. (M1)
//
// Skeleton classes are laid out for you; you write every body. Each class
// holds a FileDescriptor (RAII) rather than a raw int. Open files with
// O_CLOEXEC (why does a storage engine care about fork+exec?).

#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <memory>
#include <string>

#include "fs/file_descriptor.h"
#include "strata/file_system.h"
#include "strata/todo.h"

namespace strata {
namespace {

// ---------------------------------------------------------------------------
// SequentialFile: read(2) in a loop. Handle EINTR. Short reads are normal.
// ---------------------------------------------------------------------------
class PosixSequentialFile final : public SequentialFile {
 public:
  PosixSequentialFile(Path path, FileDescriptor fd) : path_(std::move(path)), fd_(std::move(fd)) {}

  Result<std::string_view> Read(std::span<char> /*scratch*/) override {
    STRATA_TODO("M1", "PosixSequentialFile::Read");
  }

  Status Skip(std::uint64_t /*n*/) override { STRATA_TODO("M1", "PosixSequentialFile::Skip"); }

 private:
  Path path_;
  FileDescriptor fd_;
};

// ---------------------------------------------------------------------------
// RandomAccessFile via pread(2). Must be thread-safe: no shared file offset.
// ---------------------------------------------------------------------------
class PosixRandomAccessFile final : public RandomAccessFile {
 public:
  PosixRandomAccessFile(Path path, FileDescriptor fd, std::uint64_t size)
      : path_(std::move(path)), fd_(std::move(fd)), size_(size) {}

  Result<std::string_view> Read(std::uint64_t /*offset*/,
                                std::span<char> /*scratch*/) const override {
    STRATA_TODO("M1", "PosixRandomAccessFile::Read (pread loop until n bytes or EOF)");
  }

  std::uint64_t Size() const override { return size_; }

 private:
  Path path_;
  FileDescriptor fd_;
  std::uint64_t size_;
};

// ---------------------------------------------------------------------------
// RandomAccessFile via mmap(2). Zero-copy: Read() returns a view into the
// mapping and ignores `scratch`. The mapping is an RAII resource too
// (munmap in the destructor). Bounds-check offset+n against the size.
// What happens to a reader if the file is truncated underneath the mapping?
// (SIGBUS. Why can't that happen with SSTables in this design?)
// ---------------------------------------------------------------------------
class PosixMmapReadableFile final : public RandomAccessFile {
 public:
  // TODO(M1): constructor taking ownership of the mapping; destructor munmaps.

  Result<std::string_view> Read(std::uint64_t /*offset*/,
                                std::span<char> /*scratch*/) const override {
    STRATA_TODO("M1", "PosixMmapReadableFile::Read");
  }

  std::uint64_t Size() const override { STRATA_TODO("M1", "PosixMmapReadableFile::Size"); }
};

// ---------------------------------------------------------------------------
// WritableFile: user-space buffer of `writable_buffer_size` bytes.
//   Append: copy into the buffer; if it would overflow, write out. Large
//           appends (> buffer) should bypass the buffer entirely.
//   Flush:  write(2) the buffer, looping on partial writes and EINTR.
//   Sync:   Flush + SyncFd(method). If this is a MANIFEST or other file whose
//           *directory entry* is new, is fsync of the file enough? (No: see
//           FileSystem::SyncDir and curriculum/02-crash-consistency.md.)
//   Close:  Flush + close; report errors.
// ---------------------------------------------------------------------------
class PosixWritableFile final : public WritableFile {
 public:
  PosixWritableFile(Path path, FileDescriptor fd, std::uint64_t initial_size,
                    const PosixFileSystemOptions& options)
      : path_(std::move(path)), fd_(std::move(fd)), size_(initial_size), options_(options) {}

  ~PosixWritableFile() override {
    // TODO(M1): best-effort Close(). Errors can't escape a destructor.
  }

  Status Append(std::string_view /*data*/) override {
    STRATA_TODO("M1", "PosixWritableFile::Append");
  }
  Status Flush() override { STRATA_TODO("M1", "PosixWritableFile::Flush"); }
  Status Sync() override { STRATA_TODO("M1", "PosixWritableFile::Sync"); }
  Status Close() override { STRATA_TODO("M1", "PosixWritableFile::Close"); }
  std::uint64_t Size() const override { return size_; }

 private:
  Path path_;
  FileDescriptor fd_;
  std::uint64_t size_;
  PosixFileSystemOptions options_;
  // TODO(M1): the buffer.
};

// ---------------------------------------------------------------------------
// FileLock via flock(2) (LOCK_EX | LOCK_NB). Unlocks by closing the fd.
// Why flock and not fcntl(F_SETLK)? Read the FileSystem::LockFile contract
// and the tests; then read `man fcntl` on "all locks associated with a file
// for a given process are removed when any file descriptor for that file is
// closed by that process". What would that do to a DB opened twice?
// ---------------------------------------------------------------------------
class PosixFileLock final : public FileLock {
 public:
  explicit PosixFileLock(FileDescriptor fd) : fd_(std::move(fd)) {}

 private:
  FileDescriptor fd_;
};

class PosixFileSystem final : public FileSystem {
 public:
  explicit PosixFileSystem(const PosixFileSystemOptions& options) : options_(options) {}

  Result<std::unique_ptr<WritableFile>> NewWritableFile(const Path& /*path*/) override {
    STRATA_TODO("M1", "PosixFileSystem::NewWritableFile (O_CREAT|O_TRUNC|O_WRONLY|O_CLOEXEC)");
  }

  Result<std::unique_ptr<WritableFile>> NewAppendableFile(const Path& /*path*/) override {
    STRATA_TODO("M1", "PosixFileSystem::NewAppendableFile (O_APPEND; size from fstat)");
  }

  Result<std::unique_ptr<SequentialFile>> NewSequentialFile(const Path& /*path*/) override {
    STRATA_TODO("M1", "PosixFileSystem::NewSequentialFile");
  }

  Result<std::unique_ptr<RandomAccessFile>> NewRandomAccessFile(const Path& /*path*/) override {
    STRATA_TODO("M1", "PosixFileSystem::NewRandomAccessFile (pread or mmap per options_)");
  }

  bool FileExists(const Path& /*path*/) override {
    STRATA_TODO("M1", "PosixFileSystem::FileExists");
  }

  Result<std::vector<std::string>> GetChildren(const Path& /*dir*/) override {
    STRATA_TODO("M1", "PosixFileSystem::GetChildren");
  }

  Result<std::uint64_t> GetFileSize(const Path& /*path*/) override {
    STRATA_TODO("M1", "PosixFileSystem::GetFileSize");
  }

  Status RemoveFile(const Path& /*path*/) override {
    STRATA_TODO("M1", "PosixFileSystem::RemoveFile");
  }

  Status RenameFile(const Path& /*from*/, const Path& /*to*/) override {
    STRATA_TODO("M1", "PosixFileSystem::RenameFile");
  }

  Status CreateDir(const Path& /*dir*/) override {
    STRATA_TODO("M1", "PosixFileSystem::CreateDir");
  }

  Status RemoveDir(const Path& /*dir*/) override {
    STRATA_TODO("M1", "PosixFileSystem::RemoveDir");
  }

  Status SyncDir(const Path& /*dir*/) override {
    STRATA_TODO("M1", "PosixFileSystem::SyncDir (open O_RDONLY|O_DIRECTORY, SyncFd)");
  }

  Result<std::unique_ptr<FileLock>> LockFile(const Path& /*path*/) override {
    STRATA_TODO("M1", "PosixFileSystem::LockFile");
  }

 private:
  PosixFileSystemOptions options_;
};

}  // namespace

std::unique_ptr<FileSystem> NewPosixFileSystem(const PosixFileSystemOptions& options) {
  return std::make_unique<PosixFileSystem>(options);
}

// TODO(M1, C++): return a process-wide instance that is created on first use,
// thread-safely, and never destroyed. Why "never destroyed"? (Hint: static
// destruction order vs. background threads still using it at exit.) Which
// C++11 guarantee makes the "first use" part thread-safe for free?
FileSystem* DefaultFileSystem() { STRATA_TODO("M1", "DefaultFileSystem"); }

}  // namespace strata
