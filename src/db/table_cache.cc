#include "db/table_cache.h"

#include "db/filename.h"
#include "strata/todo.h"

namespace strata {

TableCache::TableCache(Path /*dir*/, const TableOptions& /*options*/, FileSystem& /*fs*/,
                       std::size_t /*capacity*/) {
  STRATA_TODO("M5", "TableCache constructor");
}

Result<std::shared_ptr<const Table>> TableCache::FindTable(std::uint64_t /*file_number*/,
                                                           std::uint64_t /*file_size*/) {
  STRATA_TODO("M5", "TableCache::FindTable");
}

// TODO(M5): the returned iterator must keep the shared_ptr<const Table>
// alive. A tiny wrapper iterator that holds (shared_ptr<Table>, unique_ptr<Iterator>)
// works; so does a generic "iterator with a cleanup payload". Beware member
// declaration order: which one must be destroyed first?
std::unique_ptr<Iterator> TableCache::NewIterator(const ReadOptions& /*options*/,
                                                  std::uint64_t /*file_number*/,
                                                  std::uint64_t /*file_size*/) {
  STRATA_TODO("M5", "TableCache::NewIterator");
}

Result<LookupResult> TableCache::Get(const ReadOptions& /*options*/, std::uint64_t /*file_number*/,
                                     std::uint64_t /*file_size*/, const LookupKey& /*key*/) {
  STRATA_TODO("M5", "TableCache::Get");
}

void TableCache::Evict(std::uint64_t /*file_number*/) { STRATA_TODO("M5", "TableCache::Evict"); }

}  // namespace strata
