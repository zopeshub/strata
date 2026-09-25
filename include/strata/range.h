#pragma once

// TODO(M5, C++): make DB scans first-class C++20 ranges.
//
//   auto scan = strata::Scan(*db, {}, "user:100", "user:200");
//   for (auto [key, value] : scan) { ... }
//   if (!scan.status().ok()) { ... }
//
//   // and it composes with <ranges>:
//   auto first_ten = strata::Scan(*db, {}) | std::views::take(10);
//
// Requirements (tests/m5/range_test.cc checks these with concepts):
//   * ScanRange models std::ranges::input_range and std::ranges::view is NOT
//     required (it owns a strata::Iterator, so it is move-only).
//   * ScanRange::begin() returns an iterator type satisfying std::input_iterator
//     whose reference type is std::pair<std::string_view, std::string_view>.
//   * end() returns a sentinel. std::default_sentinel_t is a fine choice; ask
//     yourself why a sentinel fits a "pull from a cursor" iterator better than
//     a second iterator object.
//   * The half-open user-key interval [begin, end); nullopt means unbounded.
//   * status() reports any error the underlying strata::Iterator hit.
//
// Things to get right: what happens if begin() is called twice? What does
// operator++(int) return for an input (not forward) iterator? What must be
// true for `for (auto [k, v] : scan)` to compile?

#include <optional>
#include <string_view>

#include "strata/db.h"

namespace strata {

class ScanRange {
 public:
  // TODO(M5, C++): iterator type, begin(), end(), status(), constructor.
};

ScanRange Scan(DB& db, const ReadOptions& options,
               std::optional<std::string_view> begin = std::nullopt,
               std::optional<std::string_view> end = std::nullopt);

}  // namespace strata
