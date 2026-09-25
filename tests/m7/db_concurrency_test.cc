// Concurrency. Run these under TSan: `cmake --preset tsan && ctest --preset tsan -L m7`.
// A clean pass here without TSan proves much less than you think.

#include <atomic>
#include <chrono>

#include <gtest/gtest.h>

#include "support/db_test_util.h"
#include "support/thread_util.h"

namespace strata::testing {
namespace {

using namespace std::chrono_literals;

class DbConcurrencyTest : public DbTest {
 protected:
  DbConcurrencyTest() {
    options_.write_buffer_size = 16 << 10;
    options_.target_file_size = 16 << 10;
    options_.max_bytes_for_level_base = 64 << 10;
    options_.l0_compaction_trigger = 2;
  }
};

std::string Pad(std::uint64_t n) { return NumberedKey(n, 10); }

TEST_P(DbConcurrencyTest, ReadersNeverSeeTimeGoBackwards) {
  constexpr int kWriters = 3;
  constexpr int kReaders = 3;
  constexpr int kKeysPerWriter = 20;
  constexpr int kIters = 1500;
  auto db = Open();
  ASSERT_NE(db, nullptr);
  std::atomic<int> writers_done{0};
  RunConcurrently(kWriters + kReaders, [&](int id) {
    if (id < kWriters) {
      OnExit signal([&] { writers_done.fetch_add(1); });
      for (int i = 1; i <= kIters; i++) {
        const std::string k = "w" + std::to_string(id) + "/" + std::to_string(i % kKeysPerWriter);
        ASSERT_OK(db->Put({}, k, Pad(static_cast<std::uint64_t>(i)) + std::string(100, 'x')));
      }
      return;
    }
    Random rnd(static_cast<std::uint64_t>(id));
    std::map<std::string, std::string> last_seen;
    while (writers_done.load() < kWriters) {
      const std::string k = "w" + std::to_string(rnd.Uniform(kWriters)) + "/" +
                            std::to_string(rnd.Uniform(kKeysPerWriter));
      auto r = db->Get({}, k);
      ASSERT_TRUE(r.has_value()) << r.error().ToString();
      if (!r->has_value()) continue;
      const std::string v = (*r)->substr(0, 11);
      auto& prev = last_seen[k];
      ASSERT_GE(v, prev) << "key " << k << " went back in time";
      prev = v;
    }
  });
  ASSERT_OK(db->WaitForBackgroundWork());
}

TEST_P(DbConcurrencyTest, IteratorsAndSnapshotsSeeWholeBatches) {
  auto db = Open();
  ASSERT_NE(db, nullptr);
  std::atomic<bool> stop{false};
  RunConcurrently(4, [&](int id) {
    if (id == 0) {
      OnExit signal([&] { stop.store(true); });
      for (int i = 0; i < 1500; i++) {
        WriteBatch b;
        const std::string v = Pad(static_cast<std::uint64_t>(i));
        for (const char* k : {"a", "b", "c", "d"}) b.Put(k, v + std::string(200, 'y'));
        ASSERT_OK(db->Write({}, b));
      }
      return;
    }
    while (!stop.load()) {
      if (id == 1) {
        auto it = db->NewIterator({});
        std::string first;
        int n = 0;
        for (it->SeekToFirst(); it->Valid(); it->Next(), n++) {
          if (n == 0) first = std::string(it->value());
          ASSERT_EQ(it->value(), first) << "iterator saw a torn batch at " << it->key();
        }
        ASSERT_OK(it->status());
        ASSERT_TRUE(n == 0 || n == 4);
      } else {
        ReadOptions ro;
        ro.snapshot = db->GetSnapshot();
        auto a = db->Get(ro, "a");
        std::this_thread::yield();
        auto d = db->Get(ro, "d");
        ASSERT_TRUE(a.has_value() && d.has_value());
        ASSERT_EQ(*a, *d) << "snapshot reads disagree";
      }
    }
  });
}

TEST_P(DbConcurrencyTest, EverythingAtOnce) {
  constexpr int kWriters = 4;
  constexpr int kPerWriter = 1500;
  auto db = Open();
  ASSERT_NE(db, nullptr);
  std::atomic<int> done{0};
  RunConcurrently(kWriters + 3, [&](int id) {
    if (id < kWriters) {
      OnExit signal([&] { done.fetch_add(1); });
      Random rnd(static_cast<std::uint64_t>(id));
      for (int i = 0; i < kPerWriter; i++) {
        const std::string k = "w" + std::to_string(id) + "/" + Pad(rnd.Uniform(300));
        if (rnd.OneIn(7)) {
          ASSERT_OK(db->Delete({}, k));
        } else {
          ASSERT_OK(db->Put({}, k, RandomString(rnd, 100)));
        }
      }
      return;
    }
    Random rnd(static_cast<std::uint64_t>(id) * 13);
    while (done.load() < kWriters) {
      switch (id - kWriters) {
        case 0:
          ASSERT_OK(db->Flush());
          std::this_thread::sleep_for(1ms);
          break;
        case 1: ASSERT_OK(db->CompactRange(std::nullopt, std::nullopt)); break;
        default: {
          auto it = db->NewIterator({});
          std::string prev;
          for (it->SeekToFirst(); it->Valid(); it->Next()) {
            ASSERT_LT(prev, it->key()) << "iterator out of order or duplicate key";
            prev = std::string(it->key());
          }
          ASSERT_OK(it->status());
          static_cast<void>(db->GetProperty(properties::kStats));
          break;
        }
      }
    }
  });
  ASSERT_OK(db->WaitForBackgroundWork());

  // Replay each writer's deterministic stream to get the expected state.
  std::map<std::string, std::string> model;
  for (int id = 0; id < kWriters; id++) {
    Random rnd(static_cast<std::uint64_t>(id));
    for (int i = 0; i < kPerWriter; i++) {
      const std::string k = "w" + std::to_string(id) + "/" + Pad(rnd.Uniform(300));
      if (rnd.OneIn(7)) {
        model.erase(k);
      } else {
        model[k] = RandomString(rnd, 100);
      }
    }
  }
  EXPECT_EQ(DumpDB(*db), model);
  Reopen(db);
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(DumpDB(*db), model);
}

INSTANTIATE_TEST_SUITE_P(Fs, DbConcurrencyTest, AllFileSystems(), FsName);

class DbStallTest : public DbConcurrencyTest {};

TEST_P(DbStallTest, L0NeverExceedsStopTrigger) {
  options_.write_buffer_size = 4 << 10;
  options_.l0_compaction_trigger = 2;
  options_.l0_slowdown_writes_trigger = 3;
  options_.l0_stop_writes_trigger = 4;
  mem_->SetSyncLatency(1ms);  // slow compactions let L0 pile up
  auto db = Open();
  ASSERT_NE(db, nullptr);
  std::atomic<bool> stop{false};
  std::atomic<int> max_l0{0};
  RunConcurrently(2, [&](int id) {
    if (id == 0) {
      OnExit signal([&] { stop.store(true); });
      Random rnd(3);
      for (int i = 0; i < 3000; i++)
        ASSERT_OK(db->Put({}, Pad(rnd.Uniform(100000)), RandomString(rnd, 100)));
      return;
    }
    while (!stop.load()) {
      auto p = db->GetProperty(std::string(properties::kNumFilesAtLevelPrefix) + "0");
      ASSERT_TRUE(p.has_value());
      const int l0 = std::stoi(*p);
      int seen = max_l0.load();
      while (l0 > seen && !max_l0.compare_exchange_weak(seen, l0)) {
      }
      std::this_thread::sleep_for(200us);
    }
  });
  EXPECT_LE(max_l0.load(), options_.l0_stop_writes_trigger)
      << "writers must stall at the stop trigger";
  mem_->SetSyncLatency(0ms);
}

INSTANTIATE_TEST_SUITE_P(Fs, DbStallTest, MemOnly(), FsName);

}  // namespace
}  // namespace strata::testing
