// After M3, MemTable::Get and iterators run concurrently with one writer,
// WITHOUT a lock. Run under the tsan preset to prove it.

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "db/memtable.h"
#include "support/test_util.h"

namespace strata {
namespace {

TEST(ConcurrentMemTableTest, ReadersRaceOneWriter) {
  InternalKeyComparator icmp;
  MemTable mem(icmp);
  constexpr int kWrites = 30000;
  std::atomic<int> published{0};  // seq numbers 1..published are fully added
  std::atomic<bool> done{false};
  std::atomic<int> failures{0};

  auto value_for = [](int seq) { return "value-" + std::to_string(seq); };
  auto key_for = [](int seq) {
    return testing::NumberedKey(static_cast<std::uint64_t>(seq % 1000), 4);
  };

  auto reader = [&](std::uint64_t seed) {
    Random rnd(seed);
    while (!done.load(std::memory_order_acquire)) {
      const int n = published.load(std::memory_order_acquire);
      if (n == 0) continue;
      // Any published write must be visible, and be the newest <= its seq.
      const int seq = 1 + static_cast<int>(rnd.Uniform(static_cast<std::uint64_t>(n)));
      auto r = mem.Get(LookupKey(key_for(seq), static_cast<SequenceNumber>(seq)));
      if (!std::holds_alternative<std::string>(r) || std::get<std::string>(r) != value_for(seq))
        ++failures;
      // Iteration is sorted.
      if (rnd.OneIn(50)) {
        auto it = mem.NewIterator();
        std::string prev;
        bool first = true;
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
          if (!first && icmp.Compare(prev, it->key()) >= 0) ++failures;
          prev.assign(it->key());
          first = false;
        }
      }
      static_cast<void>(mem.ApproximateMemoryUsage());
    }
  };

  std::vector<std::thread> readers;
  for (std::uint64_t t = 0; t < 4; t++) readers.emplace_back(reader, t + 1);
  for (int seq = 1; seq <= kWrites; seq++) {
    mem.Add(static_cast<SequenceNumber>(seq), ValueType::kValue, key_for(seq), value_for(seq));
    published.store(seq, std::memory_order_release);
  }
  done.store(true, std::memory_order_release);
  for (auto& t : readers) t.join();
  EXPECT_EQ(failures.load(), 0);
}

}  // namespace
}  // namespace strata
