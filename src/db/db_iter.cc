#include "db/db_iter.h"

#include "strata/range.h"
#include "strata/todo.h"

namespace strata {

std::unique_ptr<Iterator> NewDBIterator(std::unique_ptr<Iterator> /*internal_iter*/,
                                        SequenceNumber /*sequence*/,
                                        std::shared_ptr<void> /*keepalive*/) {
  STRATA_TODO("M5", "NewDBIterator");
}

// TODO(M5, C++): see include/strata/range.h.
ScanRange Scan(DB& /*db*/, const ReadOptions& /*options*/,
               std::optional<std::string_view> /*begin*/, std::optional<std::string_view> /*end*/) {
  STRATA_TODO("M5", "strata::Scan");
}

}  // namespace strata
