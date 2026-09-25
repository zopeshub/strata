#pragma once

// WAL writer (M2). Also reused for the MANIFEST (M6): any "append records,
// recover them after a crash" file uses this format.
//
// The writer only appends; it never syncs. Durability policy (sync every
// write, group commit, sync nothing) belongs to the DB, which calls
// dest.Sync() after AddRecord when WriteOptions::sync is set.

#include <cstdint>
#include <string_view>

#include "db/log_format.h"
#include "strata/file_system.h"
#include "strata/status.h"

namespace strata::log {

class Writer {
 public:
  // `dest` must outlive the Writer and be positioned at its end.
  // `dest_length` is its current length (non-zero when reopening a log for
  // append): the writer must resume mid-block at the right offset.
  explicit Writer(WritableFile& dest, std::uint64_t dest_length = 0);

  Writer(const Writer&) = delete;
  Writer& operator=(const Writer&) = delete;

  // Appends one logical record (possibly fragmented). An empty payload is a
  // valid record and must round-trip.
  Status AddRecord(std::string_view payload);

 private:
  // TODO(M2): destination, current offset within the block, and a table of
  // precomputed CRCs of each type byte (so each fragment's CRC is
  // crc32c::Extend(type_crc[t], payload) instead of hashing the type byte
  // again). Can that table be constexpr? What does it require of crc32c?
};

}  // namespace strata::log
