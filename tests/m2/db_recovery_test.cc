// Crash recovery against simulated POWER LOSS (MemFileSystem). This is
// stricter than kill -9: after kill -9 the kernel still has your unsynced
// writes in the page cache; after power loss they are gone.
//
// The correctness criterion is the PREFIX PROPERTY: after a crash, the DB
// must contain exactly the effects of the first k operations, for some k
// that is at least the index of the last write acknowledged with sync=true.
// Never a hole, never a reordering, never garbage.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "support/db_test_util.h"

namespace strata::testing {
namespace {

struct Op {
  bool is_delete;
  std::string key;
  std::string value;
};

using State = std::map<std::string, std::string>;

void Apply(State& s, const Op& op) {
  if (op.is_delete) {
    s.erase(op.key);
  } else {
    s[op.key] = op.value;
  }
}

class DbRecoveryTest : public DbTest {
 protected:
  // Reads every key in the key space with Get (no iterators needed in M2).
  State ReadState(DB& db, int key_space) {
    State s;
    for (int i = 0; i < key_space; i++) {
      const std::string k = NumberedKey(static_cast<std::uint64_t>(i), 4);
      auto r = db.Get({}, k);
      EXPECT_TRUE(r.has_value()) << r.error().ToString();
      if (r && r->has_value()) s[k] = **r;
    }
    return s;
  }

  // Returns the k for which state == apply(ops[0..k)), searching k in
  // [min_k, ops.size()]; nullopt if there is none (prefix property violated).
  std::optional<std::size_t> MatchingPrefix(const std::vector<Op>& ops, std::size_t min_k,
                                            const State& recovered) {
    State s;
    for (std::size_t i = 0; i < min_k; i++) Apply(s, ops[i]);
    for (std::size_t k = min_k;; k++) {
      if (s == recovered) return k;
      if (k == ops.size()) return std::nullopt;
      Apply(s, ops[k]);
    }
  }
};

TEST_P(DbRecoveryTest, SyncedWritesSurvivePowerLoss) {
  auto db = Open();
  WriteOptions sync;
  sync.sync = true;
  for (int i = 0; i < 100; i++)
    ASSERT_OK(db->Put(sync, NumberedKey(i, 4), "v" + std::to_string(i)));
  CrashAndReopen(db);
  ASSERT_NE(db, nullptr);
  for (int i = 0; i < 100; i++) {
    ASSERT_EQ(GetOrDie(*db, NumberedKey(i, 4)), "v" + std::to_string(i))
        << "an acknowledged sync write was lost. Did you SyncDir after creating the WAL?";
  }
}

TEST_P(DbRecoveryTest, OneSyncedWriteMakesAllEarlierWritesDurable) {
  auto db = Open();
  for (int i = 0; i < 50; i++) ASSERT_OK(db->Put({}, NumberedKey(i, 4), "unsynced"));
  WriteOptions sync;
  sync.sync = true;
  ASSERT_OK(db->Put(sync, "zzzz", "synced"));
  CrashAndReopen(db);
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(GetOrDie(*db, "zzzz"), "synced");
  for (int i = 0; i < 50; i++) EXPECT_EQ(GetOrDie(*db, NumberedKey(i, 4)), "unsynced");
}

TEST_P(DbRecoveryTest, PrefixPropertyWithTornTails) {
  constexpr int kKeySpace = 40;
  for (std::uint64_t seed = 1; seed <= 25; seed++) {
    SCOPED_TRACE("seed=" + std::to_string(seed));
    mem_ = std::make_unique<MemFileSystem>();  // fresh "disk" per seed
    fs_ = mem_.get();
    options_.fs = fs_;
    Random rnd(seed);
    auto db = Open();
    ASSERT_NE(db, nullptr);
    std::vector<Op> ops;
    std::size_t last_synced = 0;  // ops[0..last_synced) are guaranteed durable
    const int n = 50 + static_cast<int>(rnd.Uniform(250));
    for (int i = 0; i < n; i++) {
      Op op{rnd.OneIn(5), NumberedKey(rnd.Uniform(kKeySpace), 4),
            RandomString(rnd, rnd.Skewed(10))};
      WriteOptions wo;
      wo.sync = rnd.OneIn(10);
      if (op.is_delete) {
        ASSERT_OK(db->Delete(wo, op.key));
      } else {
        ASSERT_OK(db->Put(wo, op.key, op.value));
      }
      ops.push_back(op);
      if (wo.sync) last_synced = ops.size();
    }
    const auto mode = rnd.OneIn(2) ? MemFileSystem::Unsynced::kKeepRandomPrefix
                                   : MemFileSystem::Unsynced::kDropAll;
    CrashAndReopen(db, mode, seed);
    ASSERT_NE(db, nullptr);
    const State recovered = ReadState(*db, kKeySpace);
    auto k = MatchingPrefix(ops, last_synced, recovered);
    ASSERT_TRUE(k.has_value()) << "recovered state is not any prefix of the writes "
                                  "(>= "
                               << last_synced << " of " << ops.size() << ")";
  }
}

TEST_P(DbRecoveryTest, RepeatedCrashCycles) {
  auto db = Open();
  WriteOptions sync;
  sync.sync = true;
  State model;
  for (int cycle = 0; cycle < 6; cycle++) {
    for (int i = 0; i < 30; i++) {
      const std::string k = NumberedKey(static_cast<std::uint64_t>((cycle * 7 + i) % 50), 4);
      const std::string v = "c" + std::to_string(cycle) + "i" + std::to_string(i);
      ASSERT_OK(db->Put(sync, k, v));
      model[k] = v;
    }
    CrashAndReopen(db, MemFileSystem::Unsynced::kKeepRandomPrefix,
                   static_cast<std::uint64_t>(cycle));
    ASSERT_NE(db, nullptr);
    ASSERT_EQ(ReadState(*db, 50), model) << "after crash cycle " << cycle;
  }
}

TEST_P(DbRecoveryTest, SequenceNumbersSurviveCrash) {
  auto db = Open();
  WriteOptions sync;
  sync.sync = true;
  ASSERT_OK(db->Put(sync, "k", "1"));
  CrashAndReopen(db);
  ASSERT_OK(db->Put(sync, "k", "2"));
  CrashAndReopen(db);
  EXPECT_EQ(GetOrDie(*db, "k"), "2") << "the write after recovery must win on replay";
  ASSERT_OK(db->Delete(sync, "k"));
  CrashAndReopen(db);
  EXPECT_EQ(GetOrDie(*db, "k"), "NOT_FOUND");
}

TEST_P(DbRecoveryTest, CorruptWalIsToleratedUnlessParanoid) {
  {
    auto db = Open();
    WriteOptions sync;
    sync.sync = true;
    for (int i = 0; i < 10; i++) ASSERT_OK(db->Put(sync, NumberedKey(i, 4), std::string(100, 'x')));
  }
  // Corrupt a payload byte in the biggest WAL file.
  ASSERT_OK_AND_ASSIGN(auto children, fs_->GetChildren(dbpath_));
  Path wal;
  std::uint64_t best = 0;
  for (const auto& c : children) {
    if (!c.ends_with(".log")) continue;
    ASSERT_OK_AND_ASSIGN(auto size, fs_->GetFileSize(dbpath_ / c));
    if (size > best) {
      best = size;
      wal = dbpath_ / c;
    }
  }
  ASSERT_GT(best, 500u) << "expected a WAL holding the writes";
  ASSERT_OK(mem_->CorruptByte(wal, 300));

  options_.paranoid_checks = true;
  auto paranoid = TryOpen();
  ASSERT_FALSE(paranoid.has_value()) << "paranoid_checks must refuse a corrupt WAL";
  EXPECT_TRUE(paranoid.error().IsCorruption()) << paranoid.error().ToString();

  options_.paranoid_checks = false;
  auto db = Open();
  ASSERT_NE(db, nullptr) << "non-paranoid open drops the damaged records and continues";
  ASSERT_OK(db->Put({}, "after", "corruption"));
  EXPECT_EQ(GetOrDie(*db, "after"), "corruption");
}

INSTANTIATE_TEST_SUITE_P(Fs, DbRecoveryTest, MemOnly(), FsName);

}  // namespace
}  // namespace strata::testing
