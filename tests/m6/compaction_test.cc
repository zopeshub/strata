// Leveled compaction, checked through the public API and the strata.files
// property (format in include/strata/db.h).

#include <map>
#include <sstream>

#include <gtest/gtest.h>

#include "support/db_test_util.h"

namespace strata::testing {
namespace {

struct FileInfo {
  int level;
  std::uint64_t number;
  std::uint64_t size;
  std::string smallest_hex;  // "-" for the empty key
  std::string largest_hex;
};

class CompactionTest : public DbTest {
 protected:
  CompactionTest() {
    options_.write_buffer_size = 32 << 10;
    options_.target_file_size = 32 << 10;
    options_.max_bytes_for_level_base = 128 << 10;
    options_.max_bytes_for_level_multiplier = 4;
  }

  std::vector<FileInfo> Files(DB& db) {
    auto p = db.GetProperty(properties::kFiles);
    EXPECT_TRUE(p.has_value()) << "strata.files not supported";
    std::vector<FileInfo> out;
    if (!p) return out;
    std::istringstream in(*p);
    std::string line;
    while (std::getline(in, line)) {
      if (line.empty()) continue;
      std::istringstream ls(line);
      FileInfo f;
      ls >> f.level >> f.number >> f.size >> f.smallest_hex >> f.largest_hex;
      EXPECT_FALSE(ls.fail()) << "bad strata.files line: '" << line << "'";
      out.push_back(f);
    }
    return out;
  }

  static std::string Key(const std::string& hex) { return hex == "-" ? std::string() : hex; }

  // Invariants every Version must satisfy.
  void CheckShape(DB& db) {
    const auto files = Files(db);
    std::map<int, std::vector<FileInfo>> by_level;
    for (const auto& f : files) {
      ASSERT_GE(f.level, 0);
      ASSERT_LT(f.level, 7);
      ASSERT_LE(Key(f.smallest_hex), Key(f.largest_hex)) << "file " << f.number;
      by_level[f.level].push_back(f);
    }
    for (auto& [level, v] : by_level) {
      if (level == 0) continue;
      for (std::size_t i = 1; i < v.size(); i++) {
        // Hex of bytes preserves byte order. Strict: one user key's versions
        // never span two files at L1+ (why does that matter for Get?).
        ASSERT_LT(Key(v[i - 1].largest_hex), Key(v[i].smallest_hex))
            << "overlapping files " << v[i - 1].number << " and " << v[i].number << " at L"
            << level;
      }
    }
    EXPECT_EQ(CountFilesWithSuffix(".sst"), static_cast<int>(files.size()))
        << "obsolete SSTables left on disk (or live ones missing)";
  }

  std::uint64_t LevelBytes(DB& db, int level) {
    std::uint64_t total = 0;
    for (const auto& f : Files(db)) {
      if (f.level == level) total += f.size;
    }
    return total;
  }

  void Fill(DB& db, int n, std::uint64_t seed, int value_size = 500) {
    Random rnd(seed);
    for (int i = 0; i < n; i++) {
      ASSERT_OK(db.Put({}, NumberedKey(rnd.Uniform(100000), 8),
                       RandomString(rnd, static_cast<std::size_t>(value_size))));
    }
  }
};

TEST_P(CompactionTest, L0TriggerCompactsIntoL1) {
  auto db = Open();
  for (int f = 0; f < options_.l0_compaction_trigger; f++) {
    for (int i = 0; i < 20; i++)
      ASSERT_OK(db->Put({}, NumberedKey(static_cast<std::uint64_t>(i * 10 + f), 6), "v"));
    ASSERT_OK(db->Flush());
  }
  ASSERT_OK(db->WaitForBackgroundWork());
  EXPECT_LT(NumFilesAtLevel(*db, 0), options_.l0_compaction_trigger);
  EXPECT_GT(NumFilesAtLevel(*db, 1), 0);
  CheckShape(*db);
  EXPECT_EQ(DumpDB(*db).size(), 20u * static_cast<std::size_t>(options_.l0_compaction_trigger));
}

TEST_P(CompactionTest, ShapeInvariantsUnderLoad) {
  auto db = Open();
  std::map<std::string, std::string> model;
  Random rnd(5);
  for (int i = 0; i < 6000; i++) {
    const std::string k = NumberedKey(rnd.Uniform(20000), 8);
    const std::string v = RandomString(rnd, 300);
    ASSERT_OK(db->Put({}, k, v));
    model[k] = v;
  }
  ASSERT_OK(db->WaitForBackgroundWork());
  CheckShape(*db);
  EXPECT_LT(NumFilesAtLevel(*db, 0), options_.l0_compaction_trigger);
  // After quiescence every level's score is < 1.
  double limit = static_cast<double>(options_.max_bytes_for_level_base);
  for (int level = 1; level < 6; level++) {
    EXPECT_LE(static_cast<double>(LevelBytes(*db, level)),
              limit + static_cast<double>(options_.target_file_size))
        << "L" << level << " over its size limit after WaitForBackgroundWork()";
    limit *= options_.max_bytes_for_level_multiplier;
  }
  for (const auto& f : Files(*db)) {
    if (f.level > 0) {
      EXPECT_LE(f.size, options_.target_file_size + 4096)
          << "compaction outputs are cut at target_file_size";
    }
  }
  EXPECT_EQ(DumpDB(*db), model);
  Reopen(db);
  ASSERT_NE(db, nullptr);
  CheckShape(*db);
  EXPECT_EQ(DumpDB(*db), model);
}

TEST_P(CompactionTest, CompactRangeMovesEverythingOutOfL0) {
  auto db = Open();
  Fill(*db, 2000, 1);
  ASSERT_OK(db->CompactRange(std::nullopt, std::nullopt));
  EXPECT_EQ(NumFilesAtLevel(*db, 0), 0);
  EXPECT_GT(TotalTableFiles(*db), 0);
  CheckShape(*db);
  EXPECT_GT(CounterProperty(*db, properties::kCompactionBytesWritten), 0u);
}

TEST_P(CompactionTest, OverwritesAreGarbageCollected) {
  auto db = Open();
  for (int round = 0; round < 10; round++) {
    for (int i = 0; i < 200; i++)
      ASSERT_OK(db->Put({}, NumberedKey(i, 4), std::string(500, static_cast<char>('a' + round))));
  }
  ASSERT_OK(db->CompactRange(std::nullopt, std::nullopt));
  std::uint64_t total = 0;
  for (int level = 0; level < 7; level++) total += LevelBytes(*db, level);
  EXPECT_LT(total, 200u * 500u * 2u)
      << "old versions should be gone: expected ~1x live data, not 10x";
  EXPECT_EQ(GetOrDie(*db, NumberedKey(7, 4)), std::string(500, 'j'));
}

TEST_P(CompactionTest, TombstonesAreDroppedAtTheBottom) {
  auto db = Open();
  for (int i = 0; i < 500; i++) ASSERT_OK(db->Put({}, NumberedKey(i, 4), std::string(200, 'x')));
  ASSERT_OK(db->Flush());
  for (int i = 0; i < 500; i++) ASSERT_OK(db->Delete({}, NumberedKey(i, 4)));
  ASSERT_OK(db->CompactRange(std::nullopt, std::nullopt));
  EXPECT_EQ(TotalTableFiles(*db), 0) << "every value is shadowed by a droppable tombstone";
  EXPECT_TRUE(DumpDB(*db).empty());
}

TEST_P(CompactionTest, TombstoneAboveDeeperDataIsKept) {
  // Push "victim" deep, then delete it and compact only the upper levels.
  auto db = Open();
  ASSERT_OK(db->Put({}, "victim", "old"));
  Fill(*db, 3000, 2);
  ASSERT_OK(db->CompactRange(std::nullopt, std::nullopt));
  ASSERT_OK(db->Delete({}, "victim"));
  ASSERT_OK(db->Flush());
  // More L0 files over the same range to trigger L0 -> L1 compactions.
  for (int f = 0; f < 3 * options_.l0_compaction_trigger; f++) {
    ASSERT_OK(db->Put({}, "victil", std::to_string(f)));
    ASSERT_OK(db->Put({}, "victin", std::to_string(f)));
    ASSERT_OK(db->Flush());
  }
  ASSERT_OK(db->WaitForBackgroundWork());
  EXPECT_EQ(GetOrDie(*db, "victim"), "NOT_FOUND")
      << "a tombstone was dropped while an older value lived deeper: resurrection";
  Reopen(db);
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(GetOrDie(*db, "victim"), "NOT_FOUND");
  auto dump = DumpDB(*db);
  EXPECT_FALSE(dump.contains("victim"));
}

TEST_P(CompactionTest, SnapshotsPinOldVersions) {
  auto db = Open();
  ASSERT_OK(db->Put({}, "k", "v1"));
  auto s1 = db->GetSnapshot();
  ASSERT_OK(db->Put({}, "k", "v2"));
  ASSERT_OK(db->Delete({}, "gone"));
  ASSERT_OK(db->Put({}, "gone", "before-delete"));
  auto s2 = db->GetSnapshot();
  ASSERT_OK(db->Delete({}, "k"));
  ASSERT_OK(db->Delete({}, "gone"));
  Fill(*db, 1000, 3);
  ASSERT_OK(db->CompactRange(std::nullopt, std::nullopt));
  ReadOptions r1, r2;
  r1.snapshot = s1;
  r2.snapshot = s2;
  EXPECT_EQ(GetOrDie(*db, "k", r1), "v1");
  EXPECT_EQ(GetOrDie(*db, "k", r2), "v2");
  EXPECT_EQ(GetOrDie(*db, "gone", r2), "before-delete");
  EXPECT_EQ(GetOrDie(*db, "k"), "NOT_FOUND");
  s1.reset();
  r1.snapshot.reset();
  ASSERT_OK(db->CompactRange(std::nullopt, std::nullopt));
  EXPECT_EQ(GetOrDie(*db, "k", r2), "v2") << "s2 is still alive";
  EXPECT_EQ(GetOrDie(*db, "k"), "NOT_FOUND");
}

TEST_P(CompactionTest, IteratorPinsInputFiles) {
  auto db = Open();
  Fill(*db, 2000, 4);
  ASSERT_OK(db->Flush());
  const auto before = DumpDB(*db);
  auto it = db->NewIterator({});
  Fill(*db, 2000, 5);
  ASSERT_OK(db->CompactRange(std::nullopt, std::nullopt));
  std::map<std::string, std::string> seen;
  for (it->SeekToFirst(); it->Valid(); it->Next()) seen.emplace(it->key(), it->value());
  ASSERT_OK(it->status()) << "files deleted under a live iterator?";
  EXPECT_EQ(seen, before);
  it.reset();
  ASSERT_OK(db->Put({}, "trigger", "cleanup"));
  ASSERT_OK(db->Flush());
  ASSERT_OK(db->CompactRange(std::nullopt, std::nullopt));
  CheckShape(*db);
}

TEST_P(CompactionTest, DisableAutoCompactions) {
  options_.disable_auto_compactions = true;
  auto db = Open();
  for (int f = 0; f < 2 * options_.l0_compaction_trigger; f++) {
    ASSERT_OK(db->Put({}, "k" + std::to_string(f), "v"));
    ASSERT_OK(db->Flush());
  }
  ASSERT_OK(db->WaitForBackgroundWork());
  EXPECT_EQ(NumFilesAtLevel(*db, 0), 2 * options_.l0_compaction_trigger)
      << "no automatic compaction, and no write stall either";
  ASSERT_OK(db->CompactRange(std::nullopt, std::nullopt));
  EXPECT_EQ(NumFilesAtLevel(*db, 0), 0) << "manual compaction still works";
}

INSTANTIATE_TEST_SUITE_P(Fs, CompactionTest, AllFileSystems(), FsName);

}  // namespace
}  // namespace strata::testing
