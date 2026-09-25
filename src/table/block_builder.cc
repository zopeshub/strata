#include "table/block_builder.h"

#include "strata/todo.h"
#include "util/coding.h"

namespace strata {

BlockBuilder::BlockBuilder(int /*restart_interval*/) {
  STRATA_TODO("M4", "BlockBuilder constructor");
}

void BlockBuilder::Reset() { STRATA_TODO("M4", "BlockBuilder::Reset"); }

void BlockBuilder::Add(std::string_view /*key*/, std::string_view /*value*/) {
  STRATA_TODO("M4", "BlockBuilder::Add");
}

std::string_view BlockBuilder::Finish() { STRATA_TODO("M4", "BlockBuilder::Finish"); }

std::size_t BlockBuilder::CurrentSizeEstimate() const {
  STRATA_TODO("M4", "BlockBuilder::CurrentSizeEstimate");
}

bool BlockBuilder::empty() const { STRATA_TODO("M4", "BlockBuilder::empty"); }

}  // namespace strata
