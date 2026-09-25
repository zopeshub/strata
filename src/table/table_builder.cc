#include "table/table_builder.h"

#include "strata/todo.h"
#include "table/block_builder.h"
#include "table/bloom.h"
#include "util/coding.h"
#include "util/crc32c.h"

namespace strata {

TableBuilder::TableBuilder(const TableOptions& /*options*/, WritableFile& /*file*/) {
  STRATA_TODO("M4", "TableBuilder constructor");
}

TableBuilder::~TableBuilder() = default;  // TODO(M4): assert Finish/Abandon was called

void TableBuilder::Add(std::string_view /*key*/, std::string_view /*value*/) {
  STRATA_TODO("M4", "TableBuilder::Add");
}

Status TableBuilder::status() const { STRATA_TODO("M4", "TableBuilder::status"); }

Status TableBuilder::Finish() { STRATA_TODO("M4", "TableBuilder::Finish"); }

void TableBuilder::Abandon() { STRATA_TODO("M4", "TableBuilder::Abandon"); }

std::uint64_t TableBuilder::NumEntries() const { STRATA_TODO("M4", "TableBuilder::NumEntries"); }

std::uint64_t TableBuilder::FileSize() const { STRATA_TODO("M4", "TableBuilder::FileSize"); }

}  // namespace strata
