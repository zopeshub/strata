#pragma once

// A VersionEdit is a delta between two Versions (sets of live SSTables):
// "add file 12 at L1, remove files 7 and 9 from L0, next file number is 13".
// The MANIFEST is a log (same format as the WAL) of encoded VersionEdits.
// Replaying it from the start rebuilds the current Version. (M6)
//
// Encoding: a sequence of (varint32 tag, payload) fields; see docs/design.md
// §5 for the tag table. Tagged fields make the format extensible: an old
// binary can detect (and refuse) a field it doesn't know. Is refusing the
// right call for a storage engine, as opposed to skipping it? (Checkpoint.)

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "db/dbformat.h"
#include "strata/status.h"

namespace strata {

struct FileMetaData {
  std::uint64_t number = 0;
  std::uint64_t file_size = 0;
  std::string smallest;  // internal key
  std::string largest;   // internal key

  friend bool operator==(const FileMetaData&, const FileMetaData&) = default;
};

class VersionEdit {
 public:
  void SetComparatorName(std::string_view name) { comparator_ = std::string(name); }
  void SetLogNumber(std::uint64_t n) { log_number_ = n; }
  void SetNextFileNumber(std::uint64_t n) { next_file_number_ = n; }
  void SetLastSequence(SequenceNumber s) { last_sequence_ = s; }

  void AddFile(int level, FileMetaData file) { new_files_.emplace_back(level, std::move(file)); }
  void RemoveFile(int level, std::uint64_t number) { deleted_files_.emplace(level, number); }

  [[nodiscard]] const std::optional<std::string>& comparator_name() const { return comparator_; }
  [[nodiscard]] std::optional<std::uint64_t> log_number() const { return log_number_; }
  [[nodiscard]] std::optional<std::uint64_t> next_file_number() const { return next_file_number_; }
  [[nodiscard]] std::optional<SequenceNumber> last_sequence() const { return last_sequence_; }
  [[nodiscard]] const std::vector<std::pair<int, FileMetaData>>& new_files() const {
    return new_files_;
  }
  [[nodiscard]] const std::set<std::pair<int, std::uint64_t>>& deleted_files() const {
    return deleted_files_;
  }

  void EncodeTo(std::string& dst) const;
  // Corruption on truncated fields, unknown tags, or levels >= kNumLevels.
  static Result<VersionEdit> DecodeFrom(std::string_view src);

  friend bool operator==(const VersionEdit&, const VersionEdit&) = default;

 private:
  std::optional<std::string> comparator_;
  std::optional<std::uint64_t> log_number_;
  std::optional<std::uint64_t> next_file_number_;
  std::optional<SequenceNumber> last_sequence_;
  std::vector<std::pair<int, FileMetaData>> new_files_;
  std::set<std::pair<int, std::uint64_t>> deleted_files_;
};

}  // namespace strata
