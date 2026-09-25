#pragma once

// Writes an SSTable (M4). Used by memtable flush (M5) and compaction (M6).
//
// Flow: Add() entries in strictly increasing comparator order. When the
// current data block reaches block_size, flush it and remember its handle;
// the index entry for it is written lazily when the NEXT key arrives, so the
// separator can be shortened to something in [last_key_of_block, next_key).
// Finish() writes the last data block, filter, properties, metaindex,
// index, and footer.
//
// The builder does not Sync() or Close() the file: the caller owns the
// durability decision (a flush must sync before recording the file in the
// MANIFEST; why, precisely?).

#include <cstdint>
#include <string_view>

#include "strata/file_system.h"
#include "strata/status.h"
#include "table/format.h"

namespace strata {

class TableBuilder {
 public:
  // `file` must outlive the builder.
  TableBuilder(const TableOptions& options, WritableFile& file);
  // REQUIRES: Finish() or Abandon() was called.
  ~TableBuilder();

  TableBuilder(const TableBuilder&) = delete;
  TableBuilder& operator=(const TableBuilder&) = delete;

  // REQUIRES: key > every previously added key; Finish/Abandon not called.
  void Add(std::string_view key, std::string_view value);

  // First error encountered by Add(), if any. Once non-OK, Add() is a no-op.
  [[nodiscard]] Status status() const;

  Status Finish();
  // Stop without writing the tail; the caller deletes the file.
  void Abandon();

  [[nodiscard]] std::uint64_t NumEntries() const;
  // File size so far (the final size after Finish()).
  [[nodiscard]] std::uint64_t FileSize() const;

 private:
  // TODO(M4): options, file, offset, data/index BlockBuilders, the keys
  // collected for the filter (as what type? strings copied, or views into
  // what?), pending index entry, properties, status, finished flag.
  // Consider the pimpl idiom (struct Rep; std::unique_ptr<Rep> rep_) so
  // table_builder.h stays light. What does pimpl cost at runtime?
};

}  // namespace strata
