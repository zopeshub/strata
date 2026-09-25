#include "util/arena.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "util/random.h"

namespace strata {
namespace {

TEST(ArenaTest, Empty) {
  Arena arena;
  EXPECT_LT(arena.MemoryUsage(), 64u * 1024) << "an unused arena should be (nearly) free";
}

TEST(ArenaTest, SimpleAllocationsDontOverlap) {
  Arena arena;
  std::vector<std::pair<std::size_t, char*>> allocated;
  Random rnd(301);
  std::size_t bytes = 0;
  const int n = 100000;
  for (int i = 0; i < n; i++) {
    std::size_t s;
    if (i % (n / 10) == 0) {
      s = static_cast<std::size_t>(i);
    } else {
      s = rnd.OneIn(4000) ? rnd.Uniform(6000)
                          : (rnd.OneIn(10) ? rnd.Uniform(100) : rnd.Uniform(20));
    }
    if (s == 0) s = 1;  // Our arena disallows size 0 allocations.
    char* r = rnd.OneIn(10) ? arena.AllocateAligned(s) : arena.Allocate(s);
    for (std::size_t b = 0; b < s; b++) r[b] = static_cast<char>(i % 256);  // fill with a pattern
    bytes += s;
    allocated.emplace_back(s, r);
    ASSERT_GE(arena.MemoryUsage(), bytes);
    if (i > n / 10) {
      ASSERT_LE(static_cast<double>(arena.MemoryUsage()), static_cast<double>(bytes) * 1.10);
    }
  }
  for (std::size_t i = 0; i < allocated.size(); i++) {
    const auto [num_bytes, p] = allocated[i];
    for (std::size_t b = 0; b < num_bytes; b++) {
      ASSERT_EQ(static_cast<int>(p[b]) & 0xff, static_cast<int>(i % 256))
          << "allocation " << i << " was overwritten";
    }
  }
}

TEST(ArenaTest, AlignedAllocationsAreAligned) {
  Arena arena;
  Random rnd(7);
  for (int i = 0; i < 10000; i++) {
    static_cast<void>(arena.Allocate(1 + rnd.Uniform(13)));  // knock the bump pointer off alignment
    char* p = arena.AllocateAligned(1 + rnd.Uniform(200));
    ASSERT_EQ(reinterpret_cast<std::uintptr_t>(p) % alignof(std::max_align_t), 0u);
  }
}

TEST(ArenaTest, LargeAllocations) {
  Arena arena;
  char* big = arena.Allocate(10 << 20);
  big[0] = 'a';
  big[(10 << 20) - 1] = 'z';
  char* small = arena.Allocate(16);
  small[0] = 'x';
  EXPECT_EQ(big[0], 'a');
  EXPECT_EQ(big[(10 << 20) - 1], 'z');
  EXPECT_GE(arena.MemoryUsage(), static_cast<std::size_t>(10 << 20));
}

TEST(ArenaTest, MemoryUsageCanBeReadConcurrently) {
  // Run under the tsan preset: a data race on the usage counter fails here.
  Arena arena;
  std::atomic<bool> done{false};
  std::thread reader([&] {
    std::size_t last = 0;
    while (!done.load(std::memory_order_acquire)) {
      const std::size_t now = arena.MemoryUsage();
      EXPECT_GE(now, last) << "usage must be monotonic";
      last = now;
    }
  });
  for (int i = 0; i < 200000; i++)
    static_cast<void>(arena.Allocate(1 + static_cast<std::size_t>(i % 300)));
  done.store(true, std::memory_order_release);
  reader.join();
}

}  // namespace
}  // namespace strata
