#pragma once

#include <memory>
#include <string_view>

#include "strata/status.h"

namespace strata {

// Forward iterator over sorted key/value pairs. Used for memtables, blocks,
// tables, merged views, and the user-facing DB iterator.
//
// Lifetime contract (the #1 source of bugs in this project):
//   * key()/value() views are valid only until the next call that moves the
//     iterator (Seek*, Next) or until the iterator is destroyed.
//   * An iterator must not outlive the thing it iterates unless it owns (a
//     reference-counted handle to) it. Document which one each iterator does.
//
// Error contract: when an error happens, Valid() becomes false and status()
// reports it. Callers MUST check status() after a loop ends; "not Valid()"
// alone does not mean "reached the end cleanly".
class Iterator {
 public:
  Iterator() = default;
  virtual ~Iterator() = default;
  Iterator(const Iterator&) = delete;
  Iterator& operator=(const Iterator&) = delete;

  [[nodiscard]] virtual bool Valid() const = 0;
  virtual void SeekToFirst() = 0;
  // Position at the first entry with key >= target.
  virtual void Seek(std::string_view target) = 0;
  // REQUIRES: Valid().
  virtual void Next() = 0;
  // REQUIRES: Valid().
  [[nodiscard]] virtual std::string_view key() const = 0;
  // REQUIRES: Valid().
  [[nodiscard]] virtual std::string_view value() const = 0;
  [[nodiscard]] virtual Status status() const = 0;
};

// An iterator with no entries that reports `status` (OK by default).
std::unique_ptr<Iterator> NewEmptyIterator(Status status = Status::OK());

}  // namespace strata
