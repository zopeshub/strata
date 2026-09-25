// Error handling: the contract is in include/strata/db.h ("Errors (M7)").

#include <gtest/gtest.h>

#include "db/filename.h"
#include "support/db_test_util.h"

namespace strata::testing {
namespace {

class DbErrorTest : public DbTest {
 protected:
  DbErrorTest() { options_.disable_auto_compactions = true; }

  std::vector<Path> FilesWithSuffix(std::string_view suffix) {
    std::vector<Path> out;
    auto children = fs_->GetChildren(dbpath_);
    EXPECT_TRUE(children.has_value());
    for (const auto& c : *children) {
      if (c.ends_with(suffix)) out.push_back(dbpath_ / c);
    }
    return out;
  }

  static WriteOptions Sync() {
    WriteOptions wo;
    wo.sync = true;
    return wo;
  }
};

TEST_P(DbErrorTest, FailedWalSyncIsStickyAndReadsStillWork) {
  auto db = Open();
  ASSERT_OK(db->Put(Sync(), "a", "1"));
  mem_->FailNextSyncs(1);
  EXPECT_FALSE(db->Put(Sync(), "b", "2").ok());
  Status s = db->Put({}, "c", "3");
  EXPECT_FALSE(s.ok()) << "after a failed fsync the DB must refuse writes, even non-sync ones";
  EXPECT_FALSE(db->Delete({}, "a").ok());
  WriteBatch batch;
  batch.Put("d", "4");
  EXPECT_FALSE(db->Write({}, batch).ok());
  EXPECT_EQ(GetOrDie(*db, "a"), "1") << "reads keep working";
  EXPECT_EQ(GetOrDie(*db, "c"), "NOT_FOUND");
  Reopen(db);
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(GetOrDie(*db, "a"), "1");
  ASSERT_OK(db->Put(Sync(), "e", "5")) << "reopening clears the error";
}

TEST_P(DbErrorTest, FailedFlushKeepsTheWal) {
  auto db = Open();
  for (int i = 0; i < 300; i++) ASSERT_OK(db->Put(Sync(), NumberedKey(i), "v"));
  mem_->FailNextSyncs(1);
  Status s = db->Flush();
  EXPECT_FALSE(s.ok()) << "the SSTable sync failed; the flush must report it";
  for (int i = 0; i < 300; i += 37) EXPECT_EQ(GetOrDie(*db, NumberedKey(i)), "v");
  EXPECT_FALSE(db->Put({}, "x", "y").ok()) << "a background error is sticky";
  CrashAndReopen(db);
  ASSERT_NE(db, nullptr);
  for (int i = 0; i < 300; i++) {
    ASSERT_EQ(GetOrDie(*db, NumberedKey(i)), "v")
        << "the WAL was deleted before its memtable was durable";
  }
  ASSERT_OK(db->Flush());
  EXPECT_EQ(CountFilesWithSuffix(".sst"), TotalTableFiles(*db))
      << "orphaned output from the failed flush";
}

TEST_P(DbErrorTest, FailedCompactionLosesNothing) {
  auto db = Open();
  std::map<std::string, std::string> model;
  Random rnd(9);
  for (int f = 0; f < 5; f++) {
    for (int i = 0; i < 200; i++) {
      const std::string k = NumberedKey(rnd.Uniform(1000));
      const std::string v = RandomString(rnd, 100);
      ASSERT_OK(db->Put({}, k, v));
      model[k] = v;
    }
    ASSERT_OK(db->Flush());
  }
  mem_->FailNextSyncs(1);
  EXPECT_FALSE(db->CompactRange(std::nullopt, std::nullopt).ok());
  EXPECT_EQ(DumpDB(*db), model) << "the old Version must stay installed";
  CrashAndReopen(db, MemFileSystem::Unsynced::kKeepRandomPrefix, 1);
  ASSERT_NE(db, nullptr);
  EXPECT_EQ(DumpDB(*db), model);
  EXPECT_EQ(CountFilesWithSuffix(".sst"), TotalTableFiles(*db))
      << "Open() must remove orphaned outputs";
  ASSERT_OK(db->CompactRange(std::nullopt, std::nullopt));
  EXPECT_EQ(DumpDB(*db), model);
}

TEST_P(DbErrorTest, ChecksumMismatchInTableIsCorruption) {
  {
    auto db = Open();
    for (int i = 0; i < 500; i++) ASSERT_OK(db->Put({}, NumberedKey(i), std::string(50, 'z')));
    ASSERT_OK(db->Flush());
  }
  auto tables = FilesWithSuffix(".sst");
  ASSERT_EQ(tables.size(), 1u);
  ASSERT_OK(mem_->CorruptByte(tables[0], 20));  // inside the first data block
  auto db = Open();
  ASSERT_NE(db, nullptr);
  ReadOptions verify;
  verify.verify_checksums = true;
  auto r = db->Get(verify, NumberedKey(0));
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsCorruption()) << r.error().ToString();
  auto it = db->NewIterator(verify);
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
  }
  EXPECT_TRUE(it->status().IsCorruption()) << it->status().ToString();
}

TEST_P(DbErrorTest, MissingTableFailsOpen) {
  {
    auto db = Open();
    ASSERT_OK(db->Put({}, "k", "v"));
    ASSERT_OK(db->Flush());
  }
  auto tables = FilesWithSuffix(".sst");
  ASSERT_EQ(tables.size(), 1u);
  ASSERT_OK(fs_->RemoveFile(tables[0]));
  auto r = TryOpen();
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsCorruption()) << r.error().ToString();
}

TEST_P(DbErrorTest, MissingManifestFailsOpen) {
  {
    auto db = Open();
  }
  for (const auto& m : FilesWithSuffix("")) {
    if (m.filename().string().starts_with("MANIFEST-")) {
      ASSERT_OK(fs_->RemoveFile(m));
    }
  }
  auto r = TryOpen();
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsCorruption()) << r.error().ToString();
}

TEST_P(DbErrorTest, MalformedCurrentFailsOpen) {
  {
    auto db = Open();
  }
  for (const std::string_view garbage : {"", "MANIFEST-000001", "garbage\n", "MANIFEST-abc\n"}) {
    ASSERT_OK(mem_->WriteStringToFile(CurrentFileName(dbpath_), garbage, true));
    auto r = TryOpen();
    ASSERT_FALSE(r.has_value()) << "CURRENT = '" << garbage << "'";
    EXPECT_TRUE(r.error().IsCorruption()) << r.error().ToString();
  }
}

INSTANTIATE_TEST_SUITE_P(Fs, DbErrorTest, MemOnly(), FsName);

}  // namespace
}  // namespace strata::testing
