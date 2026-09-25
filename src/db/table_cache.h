#pragma once

// Cache of open Tables keyed by file number. (M5; LRU eviction is M7)
//
// Opening a Table costs a footer read + index + filter reads, so we keep
// them open. Thread-safe. Returns shared_ptr so an entry can be evicted
// while a reader still uses it (the Table dies with its last user).
//
// M5: an unbounded std::unordered_map under a std::mutex is fine.
// M7 stretch: bounded LRU with sharding (N independent mutex+map shards,
// shard = hash(file_number) % N) to cut lock contention. Measure first.

#include <cstddef>
#include <cstdint>
#include <memory>

#include "db/dbformat.h"
#include "strata/file_system.h"
#include "strata/iterator.h"
#include "strata/options.h"
#include "strata/status.h"
#include "table/table.h"

namespace strata {

class TableCache {
 public:
  TableCache(Path dir, const TableOptions& options, FileSystem& fs, std::size_t capacity);

  TableCache(const TableCache&) = delete;
  TableCache& operator=(const TableCache&) = delete;

  Result<std::shared_ptr<const Table>> FindTable(std::uint64_t file_number,
                                                 std::uint64_t file_size);

  // The iterator keeps its Table alive.
  std::unique_ptr<Iterator> NewIterator(const ReadOptions& options, std::uint64_t file_number,
                                        std::uint64_t file_size);

  Result<LookupResult> Get(const ReadOptions& options, std::uint64_t file_number,
                           std::uint64_t file_size, const LookupKey& key);

  // Drop the cached entry for a deleted file.
  void Evict(std::uint64_t file_number);

 private:
  // TODO(M5): dir, options, fs, mutex, map<file_number, shared_ptr<const Table>>.
};

}  // namespace strata
