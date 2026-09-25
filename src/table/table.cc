#include "table/table.h"

#include "strata/todo.h"
#include "table/block.h"
#include "table/bloom.h"
#include "util/coding.h"

namespace strata {

Result<std::unique_ptr<Table>> Table::Open(const TableOptions& /*options*/,
                                           std::unique_ptr<RandomAccessFile> /*file*/,
                                           std::uint64_t /*file_size*/) {
  STRATA_TODO("M4", "Table::Open (footer -> index -> metaindex -> filter, properties)");
}

Table::~Table() = default;

// TODO(M4): a TwoLevelIterator (index -> data block). It owns the current
// data Block (it read it), and re-reads when moving to the next block. Skip
// empty data blocks. Propagate ReadBlock errors via status().
std::unique_ptr<Iterator> Table::NewIterator(const ReadOptions& /*options*/) const {
  STRATA_TODO("M4", "Table::NewIterator");
}

Result<LookupResult> Table::Get(const ReadOptions& /*options*/, const LookupKey& /*key*/) const {
  STRATA_TODO("M4", "Table::Get");
}

const TableProperties& Table::properties() const { STRATA_TODO("M4", "Table::properties"); }

Table::Stats Table::stats() const { STRATA_TODO("M4", "Table::stats"); }

}  // namespace strata
