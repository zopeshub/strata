#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "strata/iterator.h"
#include "strata/options.h"
#include "strata/status.h"
#include "strata/write_batch.h"

namespace strata {

// A consistent read view. Obtained from DB::GetSnapshot(); released when the
// last shared_ptr goes away (so the DB must learn about that release: think
// custom deleters). Must not outlive the DB.
class Snapshot {
 public:
  virtual ~Snapshot() = default;
  [[nodiscard]] virtual std::uint64_t sequence() const = 0;
};

// Property names for DB::GetProperty(). Tests and benchmarks rely on the
// exact formats documented here.
namespace properties {
// Append a level number: "strata.num-files-at-level0" -> decimal count.
inline constexpr std::string_view kNumFilesAtLevelPrefix = "strata.num-files-at-level";
// One line per live SSTable, sorted by (level, smallest key):
//   "<level> <file_number> <file_size> <smallest_user_key_hex> <largest_user_key_hex>\n"
// Keys are lowercase hex of the *user* key bytes (empty key -> "-").
inline constexpr std::string_view kFiles = "strata.files";
// Free-form, human-readable multi-line summary (levels, sizes, amplification).
inline constexpr std::string_view kStats = "strata.stats";
// Decimal counters since Open():
inline constexpr std::string_view kMemtableBytes = "strata.memtable-bytes";
inline constexpr std::string_view kWalSyncs = "strata.wal-syncs";
inline constexpr std::string_view kUserBytesWritten = "strata.user-bytes-written";
inline constexpr std::string_view kWalBytesWritten = "strata.wal-bytes-written";
inline constexpr std::string_view kFlushBytesWritten = "strata.flush-bytes-written";
inline constexpr std::string_view kCompactionBytesRead = "strata.compaction-bytes-read";
inline constexpr std::string_view kCompactionBytesWritten = "strata.compaction-bytes-written";
}  // namespace properties

// The engine. Thread-safe: any number of threads may call any method
// concurrently. All iterators and snapshots must be destroyed before the DB.
// Destroying the DB does NOT flush the memtable: the next Open() replays the
// WAL. (Keeps close fast, and means recovery code runs on every restart.)
//
// Errors (M7): if a WAL sync, flush, or compaction fails, the DB records a
// sticky background error. From then on every write (and Flush/CompactRange)
// returns that error; reads keep working. The only way out is to close and
// reopen. (Why not just retry the fsync? curriculum/07-fsync-semantics.md.)
// Open() fails with Corruption if CURRENT is malformed, names a missing
// MANIFEST, or the MANIFEST references a missing SSTable. Open() deletes
// files no live Version references (leftovers from a crashed flush or
// compaction).
class DB {
 public:
  // Opens (and recovers) the database in directory `dir`.
  static Result<std::unique_ptr<DB>> Open(const Options& options, const std::filesystem::path& dir);

  // Deletes all database files in `dir` (names ParseFileName() recognizes),
  // then the directory itself if it is empty. OK if `dir` does not exist.
  // The DB must not be open (take the LOCK to be sure).
  static Status Destroy(const Options& options, const std::filesystem::path& dir);

  DB() = default;
  virtual ~DB() = default;
  DB(const DB&) = delete;
  DB& operator=(const DB&) = delete;

  virtual Status Put(const WriteOptions& options, std::string_view key, std::string_view value) = 0;
  virtual Status Delete(const WriteOptions& options, std::string_view key) = 0;
  // Applies `batch` atomically. May overwrite the batch's sequence header.
  virtual Status Write(const WriteOptions& options, WriteBatch& batch) = 0;

  // A missing key is not an error: it is an OK Result holding nullopt.
  virtual Result<std::optional<std::string>> Get(const ReadOptions& options,
                                                 std::string_view key) = 0;

  // Iterator over user keys (latest visible version, deletions hidden).
  virtual std::unique_ptr<Iterator> NewIterator(const ReadOptions& options) = 0;

  virtual std::shared_ptr<const Snapshot> GetSnapshot() = 0;

  virtual std::optional<std::string> GetProperty(std::string_view name) = 0;

  // Flushes the mutable memtable to an L0 SSTable and waits for it. (M5)
  virtual Status Flush() = 0;
  // Compacts all data overlapping [begin, end] (nullopt = unbounded) down the
  // tree as far as it goes, and waits. (M6)
  virtual Status CompactRange(std::optional<std::string_view> begin,
                              std::optional<std::string_view> end) = 0;
  // Blocks until no flush or compaction is scheduled or running. (M5/M6)
  virtual Status WaitForBackgroundWork() = 0;
};

}  // namespace strata
