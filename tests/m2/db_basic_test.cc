#include <gtest/gtest.h>

#include "support/db_test_util.h"

namespace strata::testing {
namespace {

class DbBasicTest : public DbTest {};

TEST_P(DbBasicTest, OpenEmpty) {
  auto db = Open();
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(GetOrDie(*db, "missing"), "NOT_FOUND");
}

TEST_P(DbBasicTest, PutGet) {
  auto db = Open();
  ASSERT_OK(db->Put({}, "foo", "v1"));
  EXPECT_EQ(GetOrDie(*db, "foo"), "v1");
  ASSERT_OK(db->Put({}, "bar", "v2"));
  EXPECT_EQ(GetOrDie(*db, "bar"), "v2");
  EXPECT_EQ(GetOrDie(*db, "foo"), "v1");
}

TEST_P(DbBasicTest, Overwrite) {
  auto db = Open();
  ASSERT_OK(db->Put({}, "k", "v1"));
  ASSERT_OK(db->Put({}, "k", "v2"));
  EXPECT_EQ(GetOrDie(*db, "k"), "v2");
}

TEST_P(DbBasicTest, Delete) {
  auto db = Open();
  ASSERT_OK(db->Put({}, "k", "v"));
  ASSERT_OK(db->Delete({}, "k"));
  EXPECT_EQ(GetOrDie(*db, "k"), "NOT_FOUND");
  ASSERT_OK(db->Delete({}, "never-existed")) << "deleting a missing key is not an error";
  ASSERT_OK(db->Put({}, "k", "again"));
  EXPECT_EQ(GetOrDie(*db, "k"), "again");
}

TEST_P(DbBasicTest, EmptyKeyAndValue) {
  auto db = Open();
  ASSERT_OK(db->Put({}, "", ""));
  auto r = db->Get({}, "");
  ASSERT_TRUE(r.has_value());
  ASSERT_TRUE(r->has_value()) << "empty value is a value, not absence";
  EXPECT_EQ(**r, "");
}

TEST_P(DbBasicTest, BinaryKeys) {
  auto db = Open();
  const std::string k1("a\0b", 3), k2("a\0c", 3), k3("\xff\xfe", 2);
  ASSERT_OK(db->Put({}, k1, "1"));
  ASSERT_OK(db->Put({}, k2, "2"));
  ASSERT_OK(db->Put({}, k3, std::string("\0\0", 2)));
  EXPECT_EQ(GetOrDie(*db, k1), "1");
  EXPECT_EQ(GetOrDie(*db, k2), "2");
  EXPECT_EQ(GetOrDie(*db, k3), std::string("\0\0", 2));
  EXPECT_EQ(GetOrDie(*db, "a"), "NOT_FOUND");
}

TEST_P(DbBasicTest, LargeValueSpansManyWalBlocks) {
  auto db = Open();
  Random rnd(1);
  const std::string big = RandomBytes(rnd, 1 << 20);
  ASSERT_OK(db->Put({}, "big", big));
  EXPECT_EQ(GetOrDie(*db, "big"), big);
  Reopen(db);
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(GetOrDie(*db, "big"), big);
}

TEST_P(DbBasicTest, WriteBatchIsAtomicAndOrdered) {
  auto db = Open();
  ASSERT_OK(db->Put({}, "a", "old"));
  WriteBatch batch;
  batch.Put("a", "1");
  batch.Put("b", "2");
  batch.Delete("a");
  batch.Put("c", "first");
  batch.Put("c", "second");
  ASSERT_OK(db->Write({}, batch));
  EXPECT_EQ(GetOrDie(*db, "a"), "NOT_FOUND");
  EXPECT_EQ(GetOrDie(*db, "b"), "2");
  EXPECT_EQ(GetOrDie(*db, "c"), "second") << "later ops in a batch win";
}

TEST_P(DbBasicTest, EmptyBatchIsOk) {
  auto db = Open();
  WriteBatch batch;
  ASSERT_OK(db->Write({}, batch));
}

TEST_P(DbBasicTest, ReopenRecoversFromWal) {
  auto db = Open();
  ASSERT_OK(db->Put({}, "foo", "v1"));
  ASSERT_OK(db->Put({}, "bar", "v2"));
  ASSERT_OK(db->Delete({}, "foo"));
  Reopen(db);
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(GetOrDie(*db, "foo"), "NOT_FOUND");
  EXPECT_EQ(GetOrDie(*db, "bar"), "v2");
}

TEST_P(DbBasicTest, ReopenManyTimesLatestWins) {
  auto db = Open();
  for (int round = 0; round < 5; round++) {
    ASSERT_OK(db->Put({}, "k", "v" + std::to_string(round)));
    ASSERT_OK(db->Put({}, "round" + std::to_string(round), "x"));
    Reopen(db);
    ASSERT_NE(db, nullptr);
    EXPECT_EQ(GetOrDie(*db, "k"), "v" + std::to_string(round))
        << "sequence numbers must keep increasing across reopens";
  }
  for (int round = 0; round < 5; round++) {
    EXPECT_EQ(GetOrDie(*db, "round" + std::to_string(round)), "x");
  }
}

TEST_P(DbBasicTest, ManyKeys) {
  auto db = Open();
  for (int i = 0; i < 10000; i++) {
    ASSERT_OK(db->Put({}, NumberedKey(i), "value-" + std::to_string(i)));
  }
  for (int i = 0; i < 10000; i += 7) {
    ASSERT_EQ(GetOrDie(*db, NumberedKey(i)), "value-" + std::to_string(i));
  }
  Reopen(db);
  ASSERT_NE(db, nullptr);
  for (int i = 0; i < 10000; i += 7) {
    ASSERT_EQ(GetOrDie(*db, NumberedKey(i)), "value-" + std::to_string(i));
  }
}

TEST_P(DbBasicTest, SyncWrites) {
  auto db = Open();
  WriteOptions sync;
  sync.sync = true;
  for (int i = 0; i < 20; i++) ASSERT_OK(db->Put(sync, NumberedKey(i), "v"));
  Reopen(db);
  ASSERT_NE(db, nullptr);
  for (int i = 0; i < 20; i++) EXPECT_EQ(GetOrDie(*db, NumberedKey(i)), "v");
}

TEST_P(DbBasicTest, ErrorIfExists) {
  {
    auto db = Open();
  }
  options_.error_if_exists = true;
  auto r = TryOpen();
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsInvalidArgument()) << r.error().ToString();
}

TEST_P(DbBasicTest, MissingWithoutCreateIfMissing) {
  options_.create_if_missing = false;
  auto r = TryOpen();
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsInvalidArgument()) << r.error().ToString();
}

TEST_P(DbBasicTest, SecondOpenIsBusy) {
  auto db = Open();
  auto second = TryOpen();
  ASSERT_FALSE(second.has_value()) << "two DB instances on one directory would corrupt it";
  EXPECT_TRUE(second.error().IsBusy()) << second.error().ToString();
  db.reset();
  auto third = TryOpen();
  EXPECT_TRUE(third.has_value()) << "closing releases the lock";
}

TEST_P(DbBasicTest, Destroy) {
  {
    auto db = Open();
    ASSERT_OK(db->Put({}, "k", "v"));
  }
  ASSERT_OK(DB::Destroy(options_, dbpath_));
  EXPECT_FALSE(fs_->FileExists(dbpath_));
  auto db = Open();
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(GetOrDie(*db, "k"), "NOT_FOUND");
}

TEST_P(DbBasicTest, DestroyWhileOpenFails) {
  auto db = Open();
  EXPECT_FALSE(DB::Destroy(options_, dbpath_).ok());
  ASSERT_OK(db->Put({}, "still", "works"));
}

TEST_P(DbBasicTest, DestroyMissingIsOk) {
  EXPECT_OK(DB::Destroy(options_, dbpath_ / "never-created"));
}

INSTANTIATE_TEST_SUITE_P(Fs, DbBasicTest, AllFileSystems(), FsName);

}  // namespace
}  // namespace strata::testing
