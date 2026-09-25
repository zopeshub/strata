#pragma once

// Leveled compaction (M6). A DESIGN SKETCH, like version_set.h.
//
// A Compaction describes one unit of work: merge `inputs(0)` (files from
// `level()`) with the overlapping `inputs(1)` (files from level()+1) into
// new files at level()+1.
//
// The merge loop (CompactionJob) walks a MergingIterator over all inputs in
// internal-key order and decides, per entry, keep or drop. The rules
// (docs/design.md §6) are where the subtle bugs live:
//   1. For each user key, keep the newest entry. Drop an older entry for
//      the same user key IF no live snapshot needs it (i.e. a newer entry
//      exists that is itself <= the smallest live snapshot sequence).
//   2. A tombstone can be dropped only if it's older than every live
//      snapshot AND no file in any deeper level could contain the key
//      ("IsBaseLevelForKey"). Otherwise dropping it RESURRECTS old data.
//   3. Cut output files at target_file_size, and also early if the output
//      would overlap too many bytes of level()+2 (grandparents); otherwise
//      the *next* compaction becomes enormous.
//   Trivial move: one input file, nothing to merge with, few grandparent
//   bytes -> just change its level in the MANIFEST, rewrite nothing.
//
// C++ focus: RAII for "in-progress" state (outputs that must be deleted if
// the job fails), std::jthread + std::stop_token for the background worker,
// std::condition_variable with predicate waits, and keeping the DB mutex
// OFF during the I/O-heavy merge.

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "db/version_edit.h"
#include "db/version_set.h"

namespace strata {

class Compaction {
 public:
  [[nodiscard]] int level() const;
  [[nodiscard]] const std::vector<std::shared_ptr<const FileMetaData>>& inputs(int which) const;
  [[nodiscard]] bool IsTrivialMove() const;
  // True if no file at levels > level()+1 can contain `user_key`.
  [[nodiscard]] bool IsBaseLevelForKey(std::string_view user_key);
  // True if the current output file should be closed before `internal_key`.
  [[nodiscard]] bool ShouldStopBefore(std::string_view internal_key);
  // Record the deletion of all inputs in `edit`.
  void AddInputDeletions(VersionEdit& edit) const;

 private:
  // TODO(M6): state (level, input version, inputs_[2], grandparents, the
  // edit, per-level cursors for IsBaseLevelForKey, overlap accounting).
};

}  // namespace strata
