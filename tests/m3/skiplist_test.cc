#include "db/skiplist.h"

#include <atomic>
#include <cstdint>
#include <set>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "util/arena.h"
#include "util/random.h"

namespace strata {
namespace {

using Key = std::uint64_t;

struct Comparator {
  int operator()(const Key& a, const Key& b) const { return a < b ? -1 : (a > b ? 1 : 0); }
};

static_assert(SkipListComparator<Comparator, Key>);

TEST(SkipListTest, Empty) {
  Arena arena;
  SkipList<Key, Comparator> list(Comparator{}, arena);
  EXPECT_FALSE(list.Contains(10));

  SkipList<Key, Comparator>::Iterator iter(&list);
  EXPECT_FALSE(iter.Valid());
  iter.SeekToFirst();
  EXPECT_FALSE(iter.Valid());
  iter.Seek(100);
  EXPECT_FALSE(iter.Valid());
  iter.SeekToLast();
  EXPECT_FALSE(iter.Valid());
}

TEST(SkipListTest, InsertAndLookup) {
  const int N = 2000;
  const int R = 5000;
  Random rnd(1000);
  std::set<Key> keys;
  Arena arena;
  SkipList<Key, Comparator> list(Comparator{}, arena);
  for (int i = 0; i < N; i++) {
    const Key key = rnd.Uniform(R);
    if (keys.insert(key).second) list.Insert(key);
  }

  for (Key i = 0; i < R; i++) {
    EXPECT_EQ(list.Contains(i), keys.contains(i)) << i;
  }

  // Simple iterator tests
  {
    SkipList<Key, Comparator>::Iterator iter(&list);
    EXPECT_FALSE(iter.Valid());

    iter.Seek(0);
    ASSERT_TRUE(iter.Valid());
    EXPECT_EQ(*(keys.begin()), iter.key());

    iter.SeekToFirst();
    ASSERT_TRUE(iter.Valid());
    EXPECT_EQ(*(keys.begin()), iter.key());

    iter.SeekToLast();
    ASSERT_TRUE(iter.Valid());
    EXPECT_EQ(*(keys.rbegin()), iter.key());
  }

  // Forward iteration test
  for (Key i = 0; i < R; i++) {
    SkipList<Key, Comparator>::Iterator iter(&list);
    iter.Seek(i);

    // Compare against model iterator
    auto model_iter = keys.lower_bound(i);
    for (int j = 0; j < 3; j++) {
      if (model_iter == keys.end()) {
        ASSERT_FALSE(iter.Valid());
        break;
      }
      ASSERT_TRUE(iter.Valid());
      ASSERT_EQ(*model_iter, iter.key());
      ++model_iter;
      iter.Next();
    }
  }

  // Backward iteration test
  {
    SkipList<Key, Comparator>::Iterator iter(&list);
    iter.SeekToLast();
    for (auto model_iter = keys.rbegin(); model_iter != keys.rend(); ++model_iter) {
      ASSERT_TRUE(iter.Valid());
      ASSERT_EQ(*model_iter, iter.key());
      iter.Prev();
    }
    ASSERT_FALSE(iter.Valid());
  }
}

TEST(SkipListTest, SortedInsertOrderAndReverseOrder) {
  Arena arena;
  SkipList<Key, Comparator> list(Comparator{}, arena);
  for (Key k = 1000; k > 0; k--) list.Insert(k * 2);
  for (Key k = 0; k < 1000; k++) list.Insert(k * 2 + 1);
  SkipList<Key, Comparator>::Iterator iter(&list);
  Key expected = 1;
  for (iter.SeekToFirst(); iter.Valid(); iter.Next()) {
    ASSERT_EQ(iter.key(), expected);
    ++expected;
  }
  EXPECT_EQ(expected, 2002u);
}

// One writer inserts; readers concurrently iterate and must always see:
//   (1) strictly increasing keys, and
//   (2) every key whose insertion was *published* before the reader started.
// (2) is exactly the release/acquire guarantee. On an Apple M-series CPU
// (weakly ordered ARM), a missing release/acquire can actually fail here;
// under the tsan preset it fails deterministically.
TEST(SkipListTest, ConcurrentReadersSeeConsistentSnapshots) {
  Arena arena;
  SkipList<Key, Comparator> list(Comparator{}, arena);

  constexpr int kKeys = 20000;
  std::vector<Key> keys(kKeys);
  Random rnd(42);
  for (int i = 0; i < kKeys; i++)
    keys[static_cast<std::size_t>(i)] = (static_cast<Key>(i) << 20) | rnd.Uniform(1 << 20);
  for (int i = kKeys - 1; i > 0; i--)
    std::swap(keys[static_cast<std::size_t>(i)],
              keys[rnd.Uniform(static_cast<std::uint64_t>(i) + 1)]);

  std::atomic<int> published{0};
  std::atomic<bool> done{false};
  std::atomic<int> failures{0};

  auto reader = [&] {
    while (!done.load(std::memory_order_acquire)) {
      const int n = published.load(std::memory_order_acquire);
      std::set<Key> must_see(keys.begin(), keys.begin() + n);
      SkipList<Key, Comparator>::Iterator iter(&list);
      bool first = true;
      Key prev = 0;
      std::size_t seen_required = 0;
      for (iter.SeekToFirst(); iter.Valid(); iter.Next()) {
        const Key k = iter.key();
        if (!first && k <= prev) ++failures;
        if (must_see.contains(k)) ++seen_required;
        prev = k;
        first = false;
      }
      if (seen_required != must_see.size()) ++failures;
      for (int probe = 0; probe < 50 && n > 0; probe++) {
        if (!list.Contains(keys[static_cast<std::size_t>(probe % n)])) ++failures;
      }
    }
  };

  std::vector<std::thread> readers;
  for (int t = 0; t < 4; t++) readers.emplace_back(reader);
  for (int i = 0; i < kKeys; i++) {
    list.Insert(keys[static_cast<std::size_t>(i)]);
    published.store(i + 1, std::memory_order_release);
  }
  done.store(true, std::memory_order_release);
  for (auto& t : readers) t.join();
  EXPECT_EQ(failures.load(), 0);
}

}  // namespace
}  // namespace strata
