#pragma once

// File naming inside a DB directory. (M2; SetCurrentFile is M6)
//
//   dir/000007.log         write-ahead log #7
//   dir/000012.sst         SSTable #12
//   dir/MANIFEST-000005    version edits log #5            (M6)
//   dir/CURRENT            "MANIFEST-000005\n"             (M6)
//   dir/LOCK               advisory lock held while open
//   dir/000013.dbtmp       temp file (renamed into place)  (M6)
//
// Numbers are formatted with at least 6 digits, zero-padded; wider numbers
// are printed in full. One counter (next_file_number) allocates numbers for
// all file kinds, so a number identifies a file uniquely.

#include <cstdint>
#include <optional>
#include <string_view>

#include "strata/file_system.h"
#include "strata/status.h"

namespace strata {

enum class FileType : std::uint8_t { kLog, kTable, kManifest, kCurrent, kLock, kTemp };

struct ParsedFileName {
  std::uint64_t number = 0;  // 0 for CURRENT and LOCK
  FileType type = FileType::kLog;
  friend bool operator==(const ParsedFileName&, const ParsedFileName&) = default;
};

[[nodiscard]] Path LogFileName(const Path& dir, std::uint64_t number);
[[nodiscard]] Path TableFileName(const Path& dir, std::uint64_t number);
[[nodiscard]] Path ManifestFileName(const Path& dir, std::uint64_t number);
[[nodiscard]] Path CurrentFileName(const Path& dir);
[[nodiscard]] Path LockFileName(const Path& dir);
[[nodiscard]] Path TempFileName(const Path& dir, std::uint64_t number);

// Parses a bare file name (no directory). nullopt for anything that isn't
// exactly one of the forms above: this runs on arbitrary directory contents.
// Use std::from_chars (why not std::stoull / strtoull?).
[[nodiscard]] std::optional<ParsedFileName> ParseFileName(std::string_view name);

// (M6) Atomically points CURRENT at MANIFEST-<manifest_number>. This is the
// commit point of every version change, so it must survive power loss at
// any instant: after a crash CURRENT names either the old or the new
// manifest, never garbage, never nothing. You know the dance
// (curriculum/02-crash-consistency.md). Get every step, in order.
Status SetCurrentFile(FileSystem& fs, const Path& dir, std::uint64_t manifest_number);

}  // namespace strata
