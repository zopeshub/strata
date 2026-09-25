#include <gtest/gtest.h>

#include "support/db_test_util.h"

namespace strata::testing {
namespace {

class DbFlushTest : public DbTest {
 protected:
  DbFlushTest() {
    options_.write_buffer_size = 64 << 10;
    options_.disable_auto_compactions = true;  // M5 has no compaction yet
  }
};

TEST_P(DbFlushTest, ExplicitFlushCreatesL0File) {
  auto db = Open();
  for (int i = 0; i < 100; i++) ASSERT_OK(db->Put({}, NumberedKey(i), "v" + std::to_string(i)));
  ASSERT_OK(db->Flush());
  EXPECT_EQ(NumFilesAtLevel(*db, 0), 1);
  EXPECT_EQ(CountFilesWithSuffix(".sst"), 1);
  for (int i = 0; i < 100; i++) ASSERT_EQ(GetOrDie(*db, NumberedKey(i)), "v" + std::to_string(i));
  Reopen(db);
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(NumFilesAtLevel(*db, 0), 1);
  for (int i = 0; i < 100; i++) ASSERT_EQ(GetOrDie(*db, NumberedKey(i)), "v" + std::to_string(i));
}

TEST_P(DbFlushTest, FlushOfEmptyMemtableIsANoop) {
  auto db = Open();
  ASSERT_OK(db->Flush());
  EXPECT_EQ(NumFilesAtLevel(*db, 0), 0);
}

TEST_P(DbFlushTest, AutomaticFlushWhenBufferFills) {
  auto db = Open();
  Random rnd(1);
  for (int i = 0; i < 1000; i++) ASSERT_OK(db->Put({}, NumberedKey(i), RandomString(rnd, 1000)));
  ASSERT_OK(db->WaitForBackgroundWork());
  EXPECT_GE(NumFilesAtLevel(*db, 0), 5) << "~1 MB through a 64 KB write buffer";
  Random check(1);
  for (int i = 0; i < 1000; i++)
    ASSERT_EQ(GetOrDie(*db, NumberedKey(i)), RandomString(check, 1000));
}

TEST_P(DbFlushTest, NewestVersionWinsAcrossLayers) {
  auto db = Open();
  ASSERT_OK(db->Put({}, "k", "sst-1"));
  ASSERT_OK(db->Flush());
  ASSERT_OK(db->Put({}, "k", "sst-2"));
  ASSERT_OK(db->Flush());
  EXPECT_EQ(GetOrDie(*db, "k"), "sst-2") << "newer L0 file must shadow older";
  ASSERT_OK(db->Put({}, "k", "mem"));
  EXPECT_EQ(GetOrDie(*db, "k"), "mem") << "memtable shadows SSTables";
  ASSERT_OK(db->Delete({}, "k"));
  EXPECT_EQ(GetOrDie(*db, "k"), "NOT_FOUND") << "memtable tombstone hides SSTable values";
  ASSERT_OK(db->Flush());
  EXPECT_EQ(GetOrDie(*db, "k"), "NOT_FOUND") << "SSTable tombstone hides older SSTable values";
  Reopen(db);
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(GetOrDie(*db, "k"), "NOT_FOUND");
}

TEST_P(DbFlushTest, ObsoleteWalsAreDeleted) {
  auto db = Open();
  for (int round = 0; round < 5; round++) {
    for (int i = 0; i < 50; i++)
      ASSERT_OK(db->Put({}, NumberedKey(i), "r" + std::to_string(round)));
    ASSERT_OK(db->Flush());
  }
  ASSERT_OK(db->WaitForBackgroundWork());
  EXPECT_LE(CountFilesWithSuffix(".log"), 1)
      << "a WAL is garbage once its memtable is in an SSTable";
}

TEST_P(DbFlushTest, ReadsDuringBackgroundFlush) {
  auto db = Open();
  Random rnd(2);
  std::map<std::string, std::string> model;
  for (int i = 0; i < 3000; i++) {
    const std::string k = NumberedKey(rnd.Uniform(500));
    const std::string v = RandomString(rnd, 200);
    ASSERT_OK(db->Put({}, k, v));
    model[k] = v;
    if (i % 100 == 0) {
      const auto& [mk, mv] = *model.begin();
      ASSERT_EQ(GetOrDie(*db, mk), mv);
    }
  }
  ASSERT_OK(db->WaitForBackgroundWork());
  for (const auto& [k, v] : model) ASSERT_EQ(GetOrDie(*db, k), v);
}

TEST_P(DbFlushTest, UnknownPropertyIsNullopt) {
  auto db = Open();
  EXPECT_FALSE(db->GetProperty("strata.no-such-property").has_value());
  EXPECT_FALSE(db->GetProperty("strata.num-files-at-level99").has_value());
  EXPECT_TRUE(db->GetProperty(properties::kStats).has_value());
}

INSTANTIATE_TEST_SUITE_P(Fs, DbFlushTest, AllFileSystems(), FsName);

class DbFlushCrashTest : public DbFlushTest {};

TEST_P(DbFlushCrashTest, FlushedDataIsDurableWithoutWalSync) {
  auto db = Open();
  for (int i = 0; i < 200; i++) ASSERT_OK(db->Put({}, NumberedKey(i), "v"));  // sync=false
  ASSERT_OK(db->Flush());
  CrashAndReopen(db);
  ASSERT_NE(db, nullptr);
  for (int i = 0; i < 200; i++) {
    ASSERT_EQ(GetOrDie(*db, NumberedKey(i)), "v")
        << "Flush() returned OK, so the SSTable (and its directory entry) must be durable";
  }
}

TEST_P(DbFlushCrashTest, CrashDuringFlushLosesNothingSynced) {
  WriteOptions sync;
  sync.sync = true;
  for (std::uint64_t seed = 0; seed < 10; seed++) {
    SCOPED_TRACE("seed " + std::to_string(seed));
    mem_ = std::make_unique<MemFileSystem>();
    fs_ = mem_.get();
    options_.fs = fs_;
    auto db = Open();
    Random rnd(seed);
    const int n = 200 + static_cast<int>(rnd.Uniform(2000));
    for (int i = 0; i < n; i++)
      ASSERT_OK(db->Put(sync, NumberedKey(static_cast<std::uint64_t>(i)), std::string(100, 'x')));
    CrashAndReopen(db, MemFileSystem::Unsynced::kKeepRandomPrefix,
                   seed);  // background flush may be mid-flight
    ASSERT_NE(db, nullptr);
    for (int i = 0; i < n; i++)
      ASSERT_EQ(GetOrDie(*db, NumberedKey(static_cast<std::uint64_t>(i))), std::string(100, 'x'));
  }
}

INSTANTIATE_TEST_SUITE_P(Fs, DbFlushCrashTest, MemOnly(), FsName);

}  // namespace
}  // namespace strata::testing
