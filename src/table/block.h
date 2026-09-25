#pragma once

// Read side of BlockBuilder. (M4)
//
// Treat block contents as UNTRUSTED (they come off disk). A malformed block
// (restart count that doesn't fit, entry lengths that run off the end) must
// produce an iterator whose status() is Corruption, never a crash or an
// out-of-bounds read. The ASan preset + tests/m4/block_test.cc's corruption
// cases are how you prove it.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "db/dbformat.h"
#include "strata/iterator.h"

namespace strata {

class Block {
 public:
  explicit Block(std::string contents);

  Block(const Block&) = delete;
  Block& operator=(const Block&) = delete;

  [[nodiscard]] std::size_t size() const;

  // The iterator BORROWS this block and `cmp`: both must outlive it.
  // Seek() binary-searches restart points, then scans.
  [[nodiscard]] std::unique_ptr<Iterator> NewIterator(const Comparator& cmp) const;

 private:
  // TODO(M4): data_, restart_offset_, num_restarts_ (validated in the ctor).
};

}  // namespace strata
