#pragma once

// Model-based randomized testing: drive the DB and a std::map with the same
// random operations (puts, deletes, gets, scans, snapshots, flushes,
// compactions, reopens) and require identical answers at every step.
//
// When this fails it prints the seed and the op index. Reproduce with
//   STRATA_MODEL_SEED=<seed> ctest -R <test> --output-on-failure

#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "support/db_test_util.h"

namespace strata::testing {

struct ModelOptions {
  int ops = 3000;
  int key_space = 400;
  bool compactions = false;  // issue CompactRange ops (M6)
  bool reopens = true;
};

class DbModelTest : public DbTest {
 protected:
  using State = std::map<std::string, std::string>;

  static std::vector<std::uint64_t> Seeds(int n) {
    if (const char* s = std::getenv("STRATA_MODEL_SEED")) return {std::strtoull(s, nullptr, 10)};
    std::vector<std::uint64_t> out;
    for (int i = 1; i <= n; i++) out.push_back(static_cast<std::uint64_t>(i) * 7919);
    return out;
  }

  void RunModel(std::uint64_t seed, const ModelOptions& mo) {
    SCOPED_TRACE("seed=" + std::to_string(seed));
    Random rnd(seed);
    auto db = Open();
    ASSERT_NE(db, nullptr);
    State model;
    struct Snap {
      std::shared_ptr<const Snapshot> snapshot;
      State state;
    };
    std::vector<Snap> snaps;

    for (int op = 0; op < mo.ops; op++) {
      SCOPED_TRACE("op=" + std::to_string(op));
      const std::string key = NumberedKey(rnd.Uniform(static_cast<std::uint64_t>(mo.key_space)), 6);
      const std::uint64_t dice = rnd.Uniform(1000);
      if (dice < 550) {
        const std::string value = RandomString(rnd, rnd.Skewed(9));
        ASSERT_OK(db->Put({}, key, value));
        model[key] = value;
      } else if (dice < 700) {
        ASSERT_OK(db->Delete({}, key));
        model.erase(key);
      } else if (dice < 740) {
        WriteBatch batch;
        const int n = 1 + static_cast<int>(rnd.Uniform(10));
        for (int i = 0; i < n; i++) {
          const std::string k =
              NumberedKey(rnd.Uniform(static_cast<std::uint64_t>(mo.key_space)), 6);
          if (rnd.OneIn(3)) {
            batch.Delete(k);
            model.erase(k);
          } else {
            const std::string v = RandomString(rnd, rnd.Skewed(7));
            batch.Put(k, v);
            model[k] = v;
          }
        }
        ASSERT_OK(db->Write({}, batch));
      } else if (dice < 900) {
        auto it = model.find(key);
        ASSERT_EQ(GetOrDie(*db, key), it == model.end() ? "NOT_FOUND" : it->second);
      } else if (dice < 930) {
        // Range check from a random start, a few steps.
        auto dbit = db->NewIterator({});
        dbit->Seek(key);
        auto mit = model.lower_bound(key);
        for (int step = 0; step < 20; step++, ++mit, dbit->Next()) {
          if (mit == model.end()) {
            ASSERT_FALSE(dbit->Valid());
            break;
          }
          ASSERT_TRUE(dbit->Valid()) << "db iterator ended early at " << mit->first;
          ASSERT_EQ(dbit->key(), mit->first);
          ASSERT_EQ(dbit->value(), mit->second);
        }
        ASSERT_OK(dbit->status());
      } else if (dice < 945) {
        if (snaps.size() < 4) {
          snaps.push_back({db->GetSnapshot(), model});
        } else {
          snaps.erase(snaps.begin() + static_cast<std::ptrdiff_t>(rnd.Uniform(snaps.size())));
        }
      } else if (dice < 960 && !snaps.empty()) {
        const Snap& s = snaps[rnd.Uniform(snaps.size())];
        ReadOptions ro;
        ro.snapshot = s.snapshot;
        auto it = s.state.find(key);
        ASSERT_EQ(GetOrDie(*db, key, ro), it == s.state.end() ? "NOT_FOUND" : it->second)
            << "snapshot read";
        if (rnd.OneIn(5)) {
          ASSERT_EQ(DumpDB(*db, ro), s.state) << "snapshot scan";
        }
      } else if (dice < 975) {
        ASSERT_OK(db->Flush());
      } else if (dice < 985 && mo.compactions) {
        std::string a = NumberedKey(rnd.Uniform(static_cast<std::uint64_t>(mo.key_space)), 6);
        std::string b = NumberedKey(rnd.Uniform(static_cast<std::uint64_t>(mo.key_space)), 6);
        if (a > b) std::swap(a, b);
        if (rnd.OneIn(3)) {
          ASSERT_OK(db->CompactRange(std::nullopt, std::nullopt));
        } else {
          ASSERT_OK(db->CompactRange(a, b));
        }
      } else if (dice < 990 && mo.reopens) {
        snaps.clear();
        Reopen(db);
        ASSERT_NE(db, nullptr);
      } else if (dice < 992) {
        ASSERT_EQ(DumpDB(*db), model) << "full scan";
      }
    }
    snaps.clear();
    ASSERT_EQ(DumpDB(*db), model) << "final full scan";
    Reopen(db);
    ASSERT_NE(db, nullptr);
    ASSERT_EQ(DumpDB(*db), model) << "full scan after final reopen";
  }
};

}  // namespace strata::testing
