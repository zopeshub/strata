#include "table/block.h"

#include "strata/todo.h"
#include "util/coding.h"

namespace strata {

Block::Block(std::string /*contents*/) {
  STRATA_TODO("M4", "Block constructor (validate trailer)");
}

std::size_t Block::size() const { STRATA_TODO("M4", "Block::size"); }

// TODO(M4): a BlockIterator class in an anonymous namespace. Keep the
// current key in a std::string you reuse (keys are prefix-compressed, so you
// must reconstruct them), and the value as a string_view into the block.
// Decoding an entry should be one small function returning std::optional of
// a struct, used by both Next() and Seek().
std::unique_ptr<Iterator> Block::NewIterator(const Comparator& /*cmp*/) const {
  STRATA_TODO("M4", "Block::NewIterator");
}

}  // namespace strata
