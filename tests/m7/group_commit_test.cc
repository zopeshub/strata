// Group commit: concurrent sync writers share WAL fsyncs.
//
// MemFileSystem sleeps 2 ms in every Sync() (like a real SSD with a
// FLUSH CACHE). 8 threads x 100 sync writes without group commit = 800
// fsyncs = 1.6 s. With a leader/follower queue, one fsync covers everyone
// who queued up while the previous one was in flight.

#include <atomic>
#include <chrono>
#include <iostream>

#include <gtest/gtest.h>

#include "support/db_test_util.h"
#include "support/thread_util.h"

namespace strata::testing {
namespace {

using namespace std::chrono_literals;

std::string ThreadKey(int t, int i) {
  return "t" + std::to_string(t) + "/" + NumberedKey(static_cast<std::uint64_t>(i), 5);
}

class GroupCommitTest : public DbTest {};

TEST_P(GroupCommitTest, SingleWriterSyncsEveryWrite) {
  auto db = Open();
  WriteOptions sync;
  sync.sync = true;
  const auto before = CounterProperty(*db, properties::kWalSyncs);
  for (int i = 0; i < 50; i++) ASSERT_OK(db->Put(sync, ThreadKey(0, i), "v"));
  EXPECT_EQ(CounterProperty(*db, properties::kWalSyncs) - before, 50u);
}

TEST_P(GroupCommitTest, NonSyncWritesNeverSync) {
  auto db = Open();
  for (int i = 0; i < 200; i++) ASSERT_OK(db->Put({}, ThreadKey(0, i), "v"));
  EXPECT_EQ(CounterProperty(*db, properties::kWalSyncs), 0u);
}

TEST_P(GroupCommitTest, ConcurrentSyncWritesShareFsyncs) {
  constexpr int kThreads = 8;
  constexpr int kPerThread = 100;
  mem_->SetSyncLatency(2ms);
  auto db = Open();
  ASSERT_NE(db, nullptr);
  WriteOptions sync;
  sync.sync = true;
  const auto before = CounterProperty(*db, properties::kWalSyncs);
  const auto t0 = std::chrono::steady_clock::now();
  RunConcurrently(kThreads, [&](int t) {
    for (int i = 0; i < kPerThread; i++)
      ASSERT_OK(db->Put(sync, ThreadKey(t, i), std::to_string(i)));
  });
  const auto elapsed = std::chrono::steady_clock::now() - t0;
  const auto syncs = CounterProperty(*db, properties::kWalSyncs) - before;
  std::cout << "[ info ] " << kThreads * kPerThread << " sync writes, " << syncs << " WAL syncs, "
            << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << " ms\n";
  EXPECT_GE(syncs, 1u);
  EXPECT_LE(syncs, static_cast<std::uint64_t>(kThreads * kPerThread / 2))
      << "no evidence of group commit: each fsync should cover several writers";

  mem_->SetSyncLatency(0ms);
  CrashAndReopen(db);
  ASSERT_NE(db, nullptr);
  for (int t = 0; t < kThreads; t++) {
    for (int i = 0; i < kPerThread; i++) {
      ASSERT_EQ(GetOrDie(*db, ThreadKey(t, i)), std::to_string(i))
          << "acknowledged sync write lost";
    }
  }
}

TEST_P(GroupCommitTest, MixedSyncAndAsyncWritersKeepPerThreadPrefix) {
  constexpr int kThreads = 8;
  constexpr int kPerThread = 150;
  mem_->SetSyncLatency(500us);
  auto db = Open();
  ASSERT_NE(db, nullptr);
  RunConcurrently(kThreads, [&](int t) {
    WriteOptions wo;
    wo.sync = (t % 2 == 0);
    for (int i = 0; i < kPerThread; i++) ASSERT_OK(db->Put(wo, ThreadKey(t, i), "v"));
  });
  mem_->SetSyncLatency(0ms);
  CrashAndReopen(db, MemFileSystem::Unsynced::kKeepRandomPrefix, 42);
  ASSERT_NE(db, nullptr);
  for (int t = 0; t < kThreads; t++) {
    int present = 0;
    bool gap = false;
    for (int i = 0; i < kPerThread; i++) {
      const bool has = GetOrDie(*db, ThreadKey(t, i)) != "NOT_FOUND";
      if (has && gap)
        FAIL() << "thread " << t << ": write " << i << " survived but an earlier one didn't";
      if (!has) gap = true;
      present += has;
    }
    if (t % 2 == 0) {
      EXPECT_EQ(present, kPerThread) << "sync thread " << t << " lost acknowledged writes";
    }
  }
}

TEST_P(GroupCommitTest, BatchesFromManyThreadsStayAtomic) {
  auto db = Open();
  ASSERT_NE(db, nullptr);
  RunConcurrently(8, [&](int t) {
    for (int i = 0; i < 200; i++) {
      WriteBatch b;
      const std::string v = std::to_string(t) + ":" + std::to_string(i);
      for (int j = 0; j < 5; j++) b.Put("b" + std::to_string(j), v);
      ASSERT_OK(db->Write({}, b));
    }
  });
  const auto dump = DumpDB(*db);
  ASSERT_EQ(dump.size(), 5u);
  for (const auto& [k, v] : dump)
    EXPECT_EQ(v, dump.begin()->second) << "batches interleaved at " << k;
}

INSTANTIATE_TEST_SUITE_P(Fs, GroupCommitTest, MemOnly(), FsName);

}  // namespace
}  // namespace strata::testing
