#pragma once

// DBImpl: where all the modules meet. Grows milestone by milestone:
//
// M2  Open: create dir, take LOCK, replay every *.log in number order into a
//     fresh memtable, recover last_sequence, start a new WAL.
//     Write: assign sequence numbers, append batch to WAL (+ Sync if
//     options.sync), apply to memtable. One std::mutex serializes writers.
//     Get: memtable only.
//     Destroy: remove files, then the dir.
//
// M3  Memtable reads become lock-free (skiplist). Get() takes the mutex only
//     long enough to copy a shared_ptr<MemTable>. Measure the difference.
//
// M5  When the memtable exceeds write_buffer_size: it becomes immutable
//     (imm_), a new WAL + memtable start, and a background thread writes imm_
//     to an L0 SSTable (sync it! sync the dir!), then deletes the old WAL.
//     Recovery without a MANIFEST: every *.sst is an L0 file. Get searches
//     mem -> imm -> L0 newest first. NewIterator merges all of them.
//     Snapshots: a std::set/multiset of live sequence numbers, released via
//     the shared_ptr deleter.
//
// M6  MANIFEST + VersionSet replace "every .sst is L0". Background leveled
//     compaction. Obsolete file deletion (only files no Version references).
//
// M7  Group commit: concurrent writers queue up; the leader merges their
//     batches into one WAL record and one Sync, then wakes the followers.
//     Write stalls at the l0_slowdown/l0_stop triggers.
//     Background error handling: after a failed WAL sync, flush, or
//     compaction, the DB refuses writes (why is "retry the fsync" wrong? see
//     fsyncgate). Contract in include/strata/db.h.
//
// Threading model (write this down in docs/adr/ when you settle it):
//   mutex_ protects: memtable pointers, sequence counters, version set,
//   background-work flags, writer queue. It is NEVER held across file I/O
//   except where the design doc says so and says why.

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "strata/db.h"

namespace strata {

class DBImpl final : public DB {
 public:
  DBImpl(const Options& options, std::filesystem::path dir);
  ~DBImpl() override;

  Status Put(const WriteOptions& options, std::string_view key, std::string_view value) override;
  Status Delete(const WriteOptions& options, std::string_view key) override;
  Status Write(const WriteOptions& options, WriteBatch& batch) override;
  Result<std::optional<std::string>> Get(const ReadOptions& options, std::string_view key) override;
  std::unique_ptr<Iterator> NewIterator(const ReadOptions& options) override;
  std::shared_ptr<const Snapshot> GetSnapshot() override;
  std::optional<std::string> GetProperty(std::string_view name) override;
  Status Flush() override;
  Status CompactRange(std::optional<std::string_view> begin,
                      std::optional<std::string_view> end) override;
  Status WaitForBackgroundWork() override;

  // Called by DB::Open after construction.
  Status Recover();

 private:
  // TODO(M2+): state. Suggested starting set for M2:
  //   Options options_; std::filesystem::path dir_; FileSystem* fs_;
  //   InternalKeyComparator icmp_; std::unique_ptr<FileLock> lock_;
  //   std::mutex mutex_; std::shared_ptr<MemTable> mem_;
  //   std::unique_ptr<WritableFile> log_file_; std::unique_ptr<log::Writer> log_;
  //   std::uint64_t log_number_; SequenceNumber last_sequence_;
  // Member declaration order == construction order == reverse destruction
  // order. The WAL writer references the WAL file: which must be declared first?
};

}  // namespace strata
