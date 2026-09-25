#pragma once

// In-memory FileSystem with a POSIX-strict durability model, for crash tests.
//
// Model:
//   * Every file ("inode") has live contents and durable contents.
//     WritableFile::Sync() copies live -> durable for that inode.
//   * The namespace (path -> inode) has a live view and a durable view per
//     directory. SyncDir(d) copies the live entries of d into the durable
//     view. Creates, renames, and unlinks are NOT durable until then.
//   * SimulatePowerLoss(): the namespace reverts to the durable view and each
//     inode reverts to its durable contents. With Unsynced::kKeepRandomPrefix,
//     an inode whose live data extends its durable data keeps a random prefix
//     of the unsynced tail, modelling a torn write that partially hit disk.
//   * Directories themselves are durable as soon as they are created (the
//     engine's crash logic is about files, not about mkdir).
//
// This is the most pessimistic behaviour POSIX allows. Real file systems
// are usually kinder (ext4 data=ordered, APFS); code that is correct here is
// correct there, and the converse is not true.
//
// Crash protocol in tests:
//   fs.Freeze();              // all further mutations fail with IOError, no effect
//   db.reset();               // destructor/background threads can't change state
//   fs.SimulatePowerLoss();
//   fs.Unfreeze();
//   reopen and verify

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "strata/file_system.h"

namespace strata::testing {

class MemFileSystem final : public FileSystem {
 public:
  enum class Unsynced { kDropAll, kKeepRandomPrefix };

  MemFileSystem();
  ~MemFileSystem() override;

  // --- FileSystem --------------------------------------------------------
  Result<std::unique_ptr<WritableFile>> NewWritableFile(const Path& path) override;
  Result<std::unique_ptr<WritableFile>> NewAppendableFile(const Path& path) override;
  Result<std::unique_ptr<SequentialFile>> NewSequentialFile(const Path& path) override;
  Result<std::unique_ptr<RandomAccessFile>> NewRandomAccessFile(const Path& path) override;
  bool FileExists(const Path& path) override;
  Result<std::vector<std::string>> GetChildren(const Path& dir) override;
  Result<std::uint64_t> GetFileSize(const Path& path) override;
  Status RemoveFile(const Path& path) override;
  Status RenameFile(const Path& from, const Path& to) override;
  Status CreateDir(const Path& dir) override;
  Status RemoveDir(const Path& dir) override;
  Status SyncDir(const Path& dir) override;
  Result<std::unique_ptr<FileLock>> LockFile(const Path& path) override;

  // --- Crash simulation ----------------------------------------------------
  void SimulatePowerLoss(Unsynced mode = Unsynced::kDropAll, std::uint64_t seed = 0);
  void Freeze();
  void Unfreeze();
  [[nodiscard]] bool frozen() const;

  // --- Fault injection -----------------------------------------------------
  // The next `n` WritableFile::Sync() calls fail with IOError (data NOT made
  // durable). n < 0 means "fail forever" until reset with 0.
  void FailNextSyncs(int n);
  // Every WritableFile::Sync() sleeps this long first (outside any lock).
  void SetSyncLatency(std::chrono::microseconds latency);

  // --- Introspection / test helpers -----------------------------------------
  [[nodiscard]] std::uint64_t file_sync_count() const;
  [[nodiscard]] std::uint64_t dir_sync_count() const;
  Result<std::string> ReadFileToString(const Path& path);
  Status WriteStringToFile(const Path& path, std::string_view contents, bool sync);
  Status CorruptByte(const Path& path, std::uint64_t offset, unsigned char xor_mask = 0x80);
  Status Truncate(const Path& path, std::uint64_t size);

 private:
  struct Inode;
  struct State;
  class WFile;
  class SFile;
  class RFile;
  class Lock;

  static std::string Key(const Path& p);
  static std::string Parent(const std::string& key);

  std::shared_ptr<State> state_;
};

}  // namespace strata::testing
