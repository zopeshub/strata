// C++20 ranges over DB scans (include/strata/range.h).
//
// The range-using code sits inside templates guarded by `if constexpr` so
// this file COMPILES before you implement ScanRange; the tests just fail
// with a clear message until the concepts are satisfied.

#include "strata/range.h"

#include <iostream>
#include <ranges>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "support/db_test_util.h"

namespace strata::testing {
namespace {

template <class R>
constexpr bool kIsInputRange = std::ranges::input_range<R>;

template <class R>
constexpr bool ReferenceIsPairOfViews() {
  if constexpr (std::ranges::input_range<R>) {
    return std::same_as<std::ranges::range_reference_t<R>,
                        std::pair<std::string_view, std::string_view>>;
  } else {
    return false;
  }
}

template <class R>
constexpr bool IteratorIsInputIterator() {
  if constexpr (std::ranges::range<R>) {
    return std::input_iterator<std::ranges::iterator_t<R>>;
  } else {
    return false;
  }
}

template <class R>
constexpr bool UsesDefaultSentinel() {
  if constexpr (std::ranges::range<R>) {
    return std::same_as<std::ranges::sentinel_t<R>, std::default_sentinel_t>;
  } else {
    return false;
  }
}

template <class R>
std::vector<std::string> CollectKeys(R&& range) {
  std::vector<std::string> out;
  if constexpr (std::ranges::input_range<R>) {
    for (auto [key, value] : range) {
      out.emplace_back(key);
      static_cast<void>(value);
    }
  } else {
    ADD_FAILURE() << "ScanRange is not an input_range yet (TODO(M5) in include/strata/range.h)";
  }
  return out;
}

template <class R>
std::vector<std::string> TakeFirst(R&& range, int n) {
  std::vector<std::string> out;
  if constexpr (std::ranges::input_range<R> && std::movable<std::remove_cvref_t<R>>) {
    for (auto [key, value] : std::forward<R>(range) | std::views::take(n)) out.emplace_back(key);
  } else {
    ADD_FAILURE() << "ScanRange must be a movable input_range to compose with views";
  }
  return out;
}

template <class R>
Status StatusOf(const R& range) {
  if constexpr (requires {
                  { range.status() } -> std::same_as<Status>;
                }) {
    return range.status();
  } else {
    return Status::NotImplemented("ScanRange::status()");
  }
}

class RangeTest : public DbTest {
 protected:
  void SetUp() override {
    db_ = Open();
    ASSERT_NE(db_, nullptr);
    for (int i = 0; i < 100; i++)
      ASSERT_OK(db_->Put({}, NumberedKey(i, 2), "v" + std::to_string(i)));
    ASSERT_OK(db_->Delete({}, NumberedKey(15, 2)));
  }
  std::unique_ptr<DB> db_;
};

TEST_P(RangeTest, SatisfiesTheRangeConcepts) {
  EXPECT_TRUE(kIsInputRange<ScanRange>);
  EXPECT_TRUE(IteratorIsInputIterator<ScanRange>());
  EXPECT_TRUE(ReferenceIsPairOfViews<ScanRange>());
  if (kIsInputRange<ScanRange> && !UsesDefaultSentinel<ScanRange>()) {
    std::cout << "[ note ] end() is not std::default_sentinel_t; be ready to defend that choice\n";
  }
  EXPECT_FALSE(std::copy_constructible<ScanRange>) << "it owns an Iterator: move-only";
  EXPECT_TRUE(std::movable<ScanRange>);
}

TEST_P(RangeTest, BoundedScanIsHalfOpen) {
  auto scan = Scan(*db_, {}, NumberedKey(10, 2), NumberedKey(20, 2));
  auto keys = CollectKeys(scan);
  std::vector<std::string> expected;
  for (int i = 10; i < 20; i++) {
    if (i != 15) expected.push_back(NumberedKey(i, 2));
  }
  EXPECT_EQ(keys, expected);
  EXPECT_OK(StatusOf(scan));
}

TEST_P(RangeTest, UnboundedScan) {
  auto keys = CollectKeys(Scan(*db_, {}));
  EXPECT_EQ(keys.size(), 99u);
  if (!keys.empty()) {
    EXPECT_EQ(keys.front(), NumberedKey(0, 2));
    EXPECT_EQ(keys.back(), NumberedKey(99, 2));
  }
}

TEST_P(RangeTest, OpenEndedScans) {
  EXPECT_EQ(CollectKeys(Scan(*db_, {}, NumberedKey(95, 2))).size(), 5u);
  EXPECT_EQ(CollectKeys(Scan(*db_, {}, std::nullopt, NumberedKey(3, 2))).size(), 3u);
  EXPECT_TRUE(CollectKeys(Scan(*db_, {}, "z")).empty());
}

TEST_P(RangeTest, ComposesWithViews) {
  EXPECT_EQ(TakeFirst(Scan(*db_, {}), 3),
            (std::vector<std::string>{NumberedKey(0, 2), NumberedKey(1, 2), NumberedKey(2, 2)}));
}

INSTANTIATE_TEST_SUITE_P(Fs, RangeTest, MemOnly(), FsName);

}  // namespace
}  // namespace strata::testing
