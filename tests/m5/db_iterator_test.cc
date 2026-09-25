#include <map>

#include <gtest/gtest.h>

#include "support/db_test_util.h"

namespace strata::testing {
namespace {

class DbIteratorTest : public DbTest {
 protected:
  DbIteratorTest() {
    options_.write_buffer_size = 32 << 10;
    options_.disable_auto_compactions = true;
  }
};

TEST_P(DbIteratorTest, Empty) {
  auto db = Open();
  auto it = db->NewIterator({});
  it->SeekToFirst();
  EXPECT_FALSE(it->Valid());
  it->Seek("a");
  EXPECT_FALSE(it->Valid());
  EXPECT_OK(it->status());
}

TEST_P(DbIteratorTest, MergesMemtableAndTablesHidingOldVersionsAndTombstones) {
  auto db = Open();
  std::map<std::string, std::string> model;
  Random rnd(11);
  for (int i = 0; i < 4000; i++) {
    const std::string k = NumberedKey(rnd.Uniform(300), 4);
    if (rnd.OneIn(4)) {
      ASSERT_OK(db->Delete({}, k));
      model.erase(k);
    } else {
      const std::string v = RandomString(rnd, rnd.Uniform(100));
      ASSERT_OK(db->Put({}, k, v));
      model[k] = v;
    }
    if (i % 1000 == 999) {
      ASSERT_OK(db->Flush());
    }
  }
  EXPECT_EQ(DumpDB(*db), model);
  Reopen(db);
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(DumpDB(*db), model);
}

TEST_P(DbIteratorTest, Seek) {
  auto db = Open();
  for (int i = 0; i < 100; i += 2) ASSERT_OK(db->Put({}, NumberedKey(i, 3), "v"));
  ASSERT_OK(db->Flush());
  for (int i = 1; i < 100; i += 4) ASSERT_OK(db->Put({}, NumberedKey(i, 3), "m"));
  auto it = db->NewIterator({});
  it->Seek(NumberedKey(50, 3));
  ASSERT_TRUE(it->Valid());
  EXPECT_EQ(it->key(), NumberedKey(50, 3));
  it->Next();
  ASSERT_TRUE(it->Valid());
  EXPECT_EQ(it->key(), NumberedKey(52, 3));
  it->Seek(NumberedKey(51, 3) + "x");
  ASSERT_TRUE(it->Valid());
  EXPECT_EQ(it->key(), NumberedKey(52, 3));
  it->Seek("zzz");
  EXPECT_FALSE(it->Valid());
  EXPECT_OK(it->status());
}

TEST_P(DbIteratorTest, IteratorIsAConsistentSnapshot) {
  auto db = Open();
  ASSERT_OK(db->Put({}, "a", "1"));
  ASSERT_OK(db->Put({}, "b", "1"));
  auto it = db->NewIterator({});
  ASSERT_OK(db->Put({}, "a", "2"));
  ASSERT_OK(db->Delete({}, "b"));
  ASSERT_OK(db->Put({}, "c", "new"));
  ASSERT_OK(db->Flush());
  it->SeekToFirst();
  EXPECT_EQ(Drain(*it), (std::vector<std::pair<std::string, std::string>>{{"a", "1"}, {"b", "1"}}))
      << "an iterator sees the DB as of its creation, even across a flush";
}

TEST_P(DbIteratorTest, SnapshotsForGetAndIterators) {
  auto db = Open();
  ASSERT_OK(db->Put({}, "k", "v1"));
  auto snap = db->GetSnapshot();
  ASSERT_NE(snap, nullptr);
  ASSERT_OK(db->Put({}, "k", "v2"));
  ASSERT_OK(db->Put({}, "new", "x"));
  ReadOptions at_snap;
  at_snap.snapshot = snap;
  EXPECT_EQ(GetOrDie(*db, "k", at_snap), "v1");
  EXPECT_EQ(GetOrDie(*db, "new", at_snap), "NOT_FOUND");
  EXPECT_EQ(GetOrDie(*db, "k"), "v2");
  ASSERT_OK(db->Flush());
  EXPECT_EQ(GetOrDie(*db, "k", at_snap), "v1") << "flush must keep versions a snapshot needs";
  EXPECT_EQ(DumpDB(*db, at_snap), (std::map<std::string, std::string>{{"k", "v1"}}));
  ASSERT_OK(db->Delete({}, "k"));
  EXPECT_EQ(GetOrDie(*db, "k", at_snap), "v1");
  snap.reset();
  at_snap.snapshot.reset();
  EXPECT_EQ(GetOrDie(*db, "k"), "NOT_FOUND");
}

TEST_P(DbIteratorTest, SnapshotSequenceIsMonotonic) {
  auto db = Open();
  auto s1 = db->GetSnapshot();
  ASSERT_OK(db->Put({}, "a", "b"));
  auto s2 = db->GetSnapshot();
  EXPECT_LT(s1->sequence(), s2->sequence());
}

TEST_P(DbIteratorTest, IteratorKeepsFlushedMemtableAlive) {
  auto db = Open();
  for (int i = 0; i < 500; i++) ASSERT_OK(db->Put({}, NumberedKey(i), std::string(100, 'a')));
  auto it = db->NewIterator({});
  for (int round = 0; round < 3; round++) {
    for (int i = 0; i < 500; i++) ASSERT_OK(db->Put({}, NumberedKey(i), std::string(100, 'b')));
    ASSERT_OK(db->Flush());
  }
  ASSERT_OK(db->WaitForBackgroundWork());
  int n = 0;
  for (it->SeekToFirst(); it->Valid(); it->Next(), n++) {
    ASSERT_EQ(it->value(), std::string(100, 'a'));
  }
  EXPECT_EQ(n, 500);
  EXPECT_OK(it->status());
}

TEST_P(DbIteratorTest, ManyVersionsOfOneKey) {
  auto db = Open();
  for (int i = 0; i < 1000; i++) {
    ASSERT_OK(db->Put({}, "hot", std::to_string(i)));
    if (i % 250 == 0) {
      ASSERT_OK(db->Flush());
    }
  }
  auto dump = DumpDB(*db);
  ASSERT_EQ(dump.size(), 1u);
  EXPECT_EQ(dump["hot"], "999");
}

INSTANTIATE_TEST_SUITE_P(Fs, DbIteratorTest, AllFileSystems(), FsName);

}  // namespace
}  // namespace strata::testing
