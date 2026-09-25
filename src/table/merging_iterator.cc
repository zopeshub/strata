#include "table/merging_iterator.h"

#include "strata/todo.h"

namespace strata {

std::unique_ptr<Iterator> NewMergingIterator(const Comparator& /*cmp*/,
                                             std::vector<std::unique_ptr<Iterator>> /*children*/) {
  STRATA_TODO("M5", "NewMergingIterator");
}

}  // namespace strata
