// The crash-consistency torture test (M6 exit criterion).
//
// Random writes, flushes, and compactions; power loss at random moments,
// including while background work is in flight and right after recovery;
// torn tails on every unsynced file (WAL, SSTable, MANIFEST, CURRENT temp).
// After every crash the PREFIX PROPERTY must hold (see m2/db_recovery_test).
//
// Reproduce one seed: STRATA_CRASH_SEED=<n> ctest -R DbCrash --output-on-failure

#include <cstdlib>
#include <map>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "support/db_test_util.h"

namespace strata::testing {
namespace {

using State = std::map<std::string, std::string>;

struct Op {
  // A batch: all or nothing.
  std::vector<std::pair<std::string, std::optional<std::string>>> writes;
};

void Apply(State& s, const Op& op) {
  for (const auto& [k, v] : op.writes) {
    if (v) {
      s[k] = *v;
    } else {
      s.erase(k);
    }
  }
}

class DbCrashTest : public DbTest {
 protected:
  DbCrashTest() {
    options_.write_buffer_size = 8 << 10;
    options_.target_file_size = 8 << 10;
    options_.max_bytes_for_level_base = 32 << 10;
    options_.max_bytes_for_level_multiplier = 3;
    options_.l0_compaction_trigger = 2;
  }

  static std::vector<std::uint64_t> Seeds(int n) {
    if (const char* s = std::getenv("STRATA_CRASH_SEED")) return {std::strtoull(s, nullptr, 10)};
    std::vector<std::uint64_t> out;
    for (int i = 1; i <= n; i++) out.push_back(static_cast<std::uint64_t>(i));
    return out;
  }

  // Finds k in [min_k, ops.size()] with apply(base, ops[0..k)) == got.
  static std::optional<std::size_t> MatchingPrefix(const State& base, const std::vector<Op>& ops,
                                                   std::size_t min_k, const State& got) {
    State s = base;
    for (std::size_t i = 0; i < min_k; i++) Apply(s, ops[i]);
    for (std::size_t k = min_k;; k++) {
      if (s == got) return k;
      if (k == ops.size()) return std::nullopt;
      Apply(s, ops[k]);
    }
  }
};

TEST_P(DbCrashTest, PrefixPropertyAcrossFlushesAndCompactions) {
  for (auto seed : Seeds(30)) {
    SCOPED_TRACE("STRATA_CRASH_SEED=" + std::to_string(seed));
    mem_ = std::make_unique<MemFileSystem>();
    fs_ = mem_.get();
    options_.fs = fs_;
    Random rnd(seed);
    auto db = Open();
    ASSERT_NE(db, nullptr);
    State durable_base;  // state known to be durable at the start of a cycle

    for (int cycle = 0; cycle < 4; cycle++) {
      SCOPED_TRACE("cycle=" + std::to_string(cycle));
      std::vector<Op> ops;
      std::size_t must_survive = 0;
      const int n = 100 + static_cast<int>(rnd.Uniform(900));
      for (int i = 0; i < n; i++) {
        Op op;
        const int writes = rnd.OneIn(5) ? 1 + static_cast<int>(rnd.Uniform(8)) : 1;
        WriteBatch batch;
        for (int w = 0; w < writes; w++) {
          std::string k = NumberedKey(rnd.Uniform(150), 4);
          if (rnd.OneIn(6)) {
            batch.Delete(k);
            op.writes.emplace_back(std::move(k), std::nullopt);
          } else {
            std::string v = RandomString(rnd, rnd.Skewed(10));
            batch.Put(k, v);
            op.writes.emplace_back(std::move(k), std::move(v));
          }
        }
        WriteOptions wo;
        wo.sync = rnd.OneIn(8);
        ASSERT_OK(db->Write(wo, batch));
        ops.push_back(std::move(op));
        if (wo.sync) must_survive = ops.size();
        if (rnd.OneIn(150)) {
          ASSERT_OK(db->Flush());
          must_survive = ops.size();  // Flush() returning OK means durable
        }
        if (rnd.OneIn(400)) {
          ASSERT_OK(db->CompactRange(std::nullopt, std::nullopt));
          must_survive = ops.size();
        }
      }

      const auto mode = rnd.OneIn(3) ? MemFileSystem::Unsynced::kDropAll
                                     : MemFileSystem::Unsynced::kKeepRandomPrefix;
      CrashAndReopen(db, mode, rnd.Next());  // background work may be mid-flight
      ASSERT_NE(db, nullptr) << "recovery failed after a crash";

      if (rnd.OneIn(3)) {
        // Crash again immediately: recovery itself must be crash-safe.
        CrashAndReopen(db, MemFileSystem::Unsynced::kKeepRandomPrefix, rnd.Next());
        ASSERT_NE(db, nullptr) << "recovery failed after crashing during/after recovery";
      }

      const State got = DumpDB(*db);
      auto k = MatchingPrefix(durable_base, ops, must_survive, got);
      ASSERT_TRUE(k.has_value()) << "prefix property violated: " << got.size()
                                 << " keys recovered, none of prefixes [" << must_survive << ", "
                                 << ops.size() << "] match";
      durable_base = got;
      // The recovered state must itself be durable now: crash with nothing new.
      CrashAndReopen(db, MemFileSystem::Unsynced::kDropAll, 0);
      ASSERT_NE(db, nullptr);
      ASSERT_EQ(DumpDB(*db), durable_base) << "recovered state was not made durable by Open()";
    }
    ASSERT_OK(db->CompactRange(std::nullopt, std::nullopt));
    ASSERT_EQ(DumpDB(*db), durable_base);
  }
}

TEST_P(DbCrashTest, CrashWithSlowSyncsWhileCompacting) {
  // Slow syncs widen the window in which a crash lands mid-compaction.
  mem_->SetSyncLatency(std::chrono::microseconds(200));
  WriteOptions sync;
  sync.sync = true;
  State model;
  auto db = Open();
  for (int cycle = 0; cycle < 5; cycle++) {
    for (int i = 0; i < 400; i++) {
      const std::string k = NumberedKey(static_cast<std::uint64_t>((i * 37 + cycle) % 300), 4);
      const std::string v = std::to_string(cycle) + ":" + std::to_string(i) + std::string(50, 'p');
      ASSERT_OK(db->Put(i % 50 == 49 ? sync : WriteOptions{}, k, v));
      model[k] = v;
    }
    ASSERT_OK(db->Put(sync, "cycle", std::to_string(cycle)));
    model["cycle"] = std::to_string(cycle);
    CrashAndReopen(db, MemFileSystem::Unsynced::kKeepRandomPrefix,
                   static_cast<std::uint64_t>(cycle));
    ASSERT_NE(db, nullptr);
    ASSERT_EQ(DumpDB(*db), model) << "every write preceded a synced one";
  }
}

TEST_P(DbCrashTest, FailedSyncIsNotAcknowledged) {
  auto db = Open();
  WriteOptions sync;
  sync.sync = true;
  ASSERT_OK(db->Put(sync, "a", "1"));
  mem_->FailNextSyncs(1);
  Status s = db->Put(sync, "b", "2");
  EXPECT_FALSE(s.ok()) << "a sync write whose fsync failed must not return OK";
  CrashAndReopen(db);
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(GetOrDie(*db, "a"), "1");
  // "b" may or may not be there; either is correct. What is NOT correct is
  // having returned OK for it (checked above).
}

INSTANTIATE_TEST_SUITE_P(Fs, DbCrashTest, MemOnly(), FsName);

}  // namespace
}  // namespace strata::testing
