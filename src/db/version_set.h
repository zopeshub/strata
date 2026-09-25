#pragma once

// Versions and the VersionSet (M6). A DESIGN SKETCH: tests exercise this
// only through the DB API, so reshape it freely, but record the reasons in
// an ADR (docs/adr/).
//
// Version: an immutable snapshot of "which SSTables are live at each level".
//   * L0 files may overlap each other; search them newest first.
//   * L1+ files at one level are disjoint and sorted by smallest key; binary
//     search to find the one file that might contain a key.
//   Readers (Get, iterators) grab a std::shared_ptr<const Version> and use it
//   without any lock. Compaction builds a NEW Version and swaps it in. Files
//   are deleted only when no Version references them any more.
//
// VersionSet: owns the current Version, the MANIFEST writer, and the
// counters (next file number, last sequence, current WAL number).
//   LogAndApply(edit): append the edit to the MANIFEST, sync it, THEN install
//   the new Version. Why must the MANIFEST write come first? What if the
//   process dies between the two steps?
//
// C++ focus (M6): shared ownership done right. shared_ptr<const Version> for
// readers; who holds the "current" pointer, and how is it swapped?
// std::atomic<std::shared_ptr<T>> would be ideal, but libc++ (macOS) does
// not implement it yet: use a mutex, and explain why reading a shared_ptr
// that another thread is assigning, without one, is a data race even though
// the control block's refcount is atomic.

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "db/dbformat.h"
#include "db/version_edit.h"
#include "strata/file_system.h"
#include "strata/iterator.h"
#include "strata/options.h"
#include "strata/status.h"

namespace strata {

class TableCache;
class Compaction;

class Version {
 public:
  Version() = default;

  [[nodiscard]] const std::vector<std::shared_ptr<const FileMetaData>>& files(int level) const {
    return files_[static_cast<std::size_t>(level)];
  }

  // Search the SSTables in this version for `key`.
  Result<LookupResult> Get(const ReadOptions& options, const LookupKey& key,
                           TableCache& cache) const;

  // Append one iterator per L0 file and one concatenating iterator per
  // non-empty level >= 1.
  void AddIterators(const ReadOptions& options, TableCache& cache,
                    std::vector<std::unique_ptr<Iterator>>& out) const;

  [[nodiscard]] std::uint64_t LevelBytes(int level) const;

 private:
  friend class VersionSet;
  std::vector<std::shared_ptr<const FileMetaData>> files_[kNumLevels];
};

class VersionSet {
 public:
  VersionSet(Path dir, const Options& options, FileSystem& fs, TableCache& cache,
             const InternalKeyComparator& icmp);

  // Reads CURRENT, replays the MANIFEST it names. Sets counters.
  Status Recover();

  // Persists `edit` (with counters filled in) to the MANIFEST and installs
  // the resulting Version as current. `lock` is the DB mutex, held on entry;
  // release it around the file I/O so readers aren't blocked on fsync.
  Status LogAndApply(VersionEdit& edit, std::unique_lock<std::mutex>& lock);

  [[nodiscard]] std::shared_ptr<const Version> current() const;

  std::uint64_t NewFileNumber();
  [[nodiscard]] SequenceNumber LastSequence() const;
  void SetLastSequence(SequenceNumber s);
  [[nodiscard]] std::uint64_t LogNumber() const;

  // Which compaction to do next (nullptr if none needed): L0 by file count,
  // L1+ by bytes / limit score.
  std::unique_ptr<Compaction> PickCompaction();
  // Numbers of every file referenced by any live Version.
  void AddLiveFiles(std::vector<std::uint64_t>& live) const;

 private:
  // TODO(M6): state.
};

}  // namespace strata
