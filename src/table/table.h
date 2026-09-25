#pragma once

// Reads an SSTable (M4). Immutable once opened, shared by many threads.
//
// Open(): read the footer, then the index block and the filter block, and
// keep them in memory. Data blocks are read on demand.
// Get(): bloom check on the user key -> index Seek -> read ONE data block ->
// Seek within it -> interpret the entry (value, tombstone, or other key).
// NewIterator(): a two-level iterator (index iterator -> data block iterator).
//
// Page-cache tie-in (curriculum/04-page-cache.md): every data block read is
// a pread through the kernel page cache. There is no user-space block cache
// yet; that's an M7 stretch goal and you'll measure why it helps.

#include <atomic>
#include <cstdint>
#include <memory>

#include "db/dbformat.h"
#include "strata/file_system.h"
#include "strata/iterator.h"
#include "strata/options.h"
#include "strata/status.h"
#include "table/format.h"

namespace strata {

struct TableProperties {
  std::uint64_t num_entries = 0;
  std::uint64_t num_data_blocks = 0;
  std::uint64_t raw_key_bytes = 0;
  std::uint64_t raw_value_bytes = 0;
  std::uint64_t index_block_size = 0;
  std::uint64_t filter_block_size = 0;
};

class Table {
 public:
  // Takes ownership of `file`. The comparator in `options` must outlive the table.
  static Result<std::unique_ptr<Table>> Open(const TableOptions& options,
                                             std::unique_ptr<RandomAccessFile> file,
                                             std::uint64_t file_size);
  ~Table();

  Table(const Table&) = delete;
  Table& operator=(const Table&) = delete;

  // Iterates internal keys. Must not outlive this Table.
  [[nodiscard]] std::unique_ptr<Iterator> NewIterator(const ReadOptions& options) const;

  // Point lookup as of key's sequence. REQUIRES: keys are internal keys.
  [[nodiscard]] Result<LookupResult> Get(const ReadOptions& options, const LookupKey& key) const;

  [[nodiscard]] const TableProperties& properties() const;

  // Counters for tests and benchmarks (thread-safe to read).
  struct Stats {
    std::uint64_t data_blocks_read = 0;
    std::uint64_t bloom_checks = 0;
    std::uint64_t bloom_negatives = 0;  // lookups answered by the filter alone
  };
  [[nodiscard]] Stats stats() const;

 private:
  // C++: Open() is the only way to construct a Table. How do you let it use
  // std::make_unique with a private constructor? (Passkey idiom, or accept
  // `new`. Pick one and say why.)
  Table() = default;

  // TODO(M4): options, file, footer, index Block, filter contents,
  // BloomFilterPolicy, properties, and std::atomic<uint64_t> counters.
  // Which memory_order do statistics counters need?
};

}  // namespace strata
