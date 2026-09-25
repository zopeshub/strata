// Self-tests for the crash-simulating MemFileSystem. These pass from day one;
// they pin down the durability model your engine is tested against, so read
// them as documentation of "what survives a power cut".

#include "support/mem_fs.h"

#include <algorithm>

#include <gtest/gtest.h>

#include "support/test_util.h"

namespace strata::testing {
namespace {

class MemFsTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_OK(fs_.CreateDir("/d")); }

  void WriteFile(const Path& p, std::string_view data, bool sync_file, bool sync_dir) {
    ASSERT_OK(fs_.WriteStringToFile(p, data, sync_file));
    if (sync_dir) {
      ASSERT_OK(fs_.SyncDir(p.parent_path()));
    }
  }

  std::string Read(const Path& p) {
    auto r = fs_.ReadFileToString(p);
    return r ? *r : "<missing>";
  }

  MemFileSystem fs_;
};

TEST_F(MemFsTest, WriteAndReadBack) {
  WriteFile("/d/a", "hello", false, false);
  EXPECT_EQ(Read("/d/a"), "hello");
  ASSERT_OK_AND_ASSIGN(auto size, fs_.GetFileSize("/d/a"));
  EXPECT_EQ(size, 5u);
}

TEST_F(MemFsTest, SyncedFileAndDirSurvive) {
  WriteFile("/d/a", "durable", true, true);
  fs_.SimulatePowerLoss();
  EXPECT_EQ(Read("/d/a"), "durable");
}

TEST_F(MemFsTest, UnsyncedAppendIsLost) {
  WriteFile("/d/a", "abc", true, true);
  ASSERT_OK_AND_ASSIGN(auto f, fs_.NewAppendableFile("/d/a"));
  ASSERT_OK(f->Append("def"));
  EXPECT_EQ(Read("/d/a"), "abcdef");
  fs_.SimulatePowerLoss();
  EXPECT_EQ(Read("/d/a"), "abc");
}

TEST_F(MemFsTest, FsyncOfNewFileWithoutDirSyncIsNotEnough) {
  WriteFile("/d/a", "data", /*sync_file=*/true, /*sync_dir=*/false);
  fs_.SimulatePowerLoss();
  EXPECT_FALSE(fs_.FileExists("/d/a")) << "a new file's directory entry needs SyncDir";
}

TEST_F(MemFsTest, DirSyncWithoutFileSyncGivesEmptyFile) {
  WriteFile("/d/a", "data", /*sync_file=*/false, /*sync_dir=*/true);
  fs_.SimulatePowerLoss();
  EXPECT_TRUE(fs_.FileExists("/d/a"));
  EXPECT_EQ(Read("/d/a"), "");
}

TEST_F(MemFsTest, RenameIsNotDurableUntilDirSync) {
  WriteFile("/d/a", "A", true, true);
  ASSERT_OK(fs_.RenameFile("/d/a", "/d/b"));
  fs_.SimulatePowerLoss();
  EXPECT_TRUE(fs_.FileExists("/d/a"));
  EXPECT_FALSE(fs_.FileExists("/d/b"));

  ASSERT_OK(fs_.RenameFile("/d/a", "/d/b"));
  ASSERT_OK(fs_.SyncDir("/d"));
  fs_.SimulatePowerLoss();
  EXPECT_FALSE(fs_.FileExists("/d/a"));
  EXPECT_EQ(Read("/d/b"), "A");
}

TEST_F(MemFsTest, RenameOverExistingIsAtomicAfterDirSync) {
  WriteFile("/d/CURRENT", "old", true, true);
  WriteFile("/d/tmp", "new", true, false);
  ASSERT_OK(fs_.RenameFile("/d/tmp", "/d/CURRENT"));
  EXPECT_EQ(Read("/d/CURRENT"), "new");
  fs_.SimulatePowerLoss();
  EXPECT_EQ(Read("/d/CURRENT"), "old") << "rename not yet durable: old contents come back";
  EXPECT_FALSE(fs_.FileExists("/d/tmp")) << "tmp's entry was never made durable";
}

TEST_F(MemFsTest, UnlinkIsNotDurableUntilDirSync) {
  WriteFile("/d/a", "A", true, true);
  ASSERT_OK(fs_.RemoveFile("/d/a"));
  EXPECT_FALSE(fs_.FileExists("/d/a"));
  fs_.SimulatePowerLoss();
  EXPECT_TRUE(fs_.FileExists("/d/a"));
}

TEST_F(MemFsTest, TruncatingRewriteRevertsToOldDurableContents) {
  WriteFile("/d/a", "old-contents", true, true);
  WriteFile("/d/a", "new", false, false);  // O_TRUNC + write, no sync
  EXPECT_EQ(Read("/d/a"), "new");
  fs_.SimulatePowerLoss();
  EXPECT_EQ(Read("/d/a"), "old-contents");
}

TEST_F(MemFsTest, KeepRandomPrefixKeepsATornTail) {
  WriteFile("/d/a", "0123456789", true, true);
  ASSERT_OK_AND_ASSIGN(auto f, fs_.NewAppendableFile("/d/a"));
  ASSERT_OK(f->Append("abcdefghijklmnopqrstuvwxyz"));
  f.reset();
  fs_.SimulatePowerLoss(MemFileSystem::Unsynced::kKeepRandomPrefix, /*seed=*/7);
  const std::string after = Read("/d/a");
  EXPECT_TRUE(after.starts_with("0123456789"));
  EXPECT_TRUE(std::string("0123456789abcdefghijklmnopqrstuvwxyz").starts_with(after));
}

TEST_F(MemFsTest, FreezeBlocksAllMutations) {
  WriteFile("/d/a", "A", true, true);
  ASSERT_OK_AND_ASSIGN(auto f, fs_.NewAppendableFile("/d/a"));
  fs_.Freeze();
  EXPECT_FALSE(f->Append("x").ok());
  EXPECT_FALSE(f->Sync().ok());
  EXPECT_FALSE(fs_.NewWritableFile("/d/b").has_value());
  EXPECT_FALSE(fs_.RenameFile("/d/a", "/d/c").ok());
  EXPECT_FALSE(fs_.RemoveFile("/d/a").ok());
  EXPECT_FALSE(fs_.SyncDir("/d").ok());
  EXPECT_EQ(Read("/d/a"), "A");
  fs_.Unfreeze();
  EXPECT_OK(f->Append("x"));
}

TEST_F(MemFsTest, InjectedSyncFailure) {
  ASSERT_OK_AND_ASSIGN(auto f, fs_.NewWritableFile("/d/a"));
  ASSERT_OK(fs_.SyncDir("/d"));
  ASSERT_OK(f->Append("abc"));
  fs_.FailNextSyncs(1);
  EXPECT_TRUE(f->Sync().IsIOError());
  fs_.SimulatePowerLoss();
  EXPECT_EQ(Read("/d/a"), "") << "a failed sync made nothing durable";
}

TEST_F(MemFsTest, LocksAreExclusiveWithinAProcess) {
  ASSERT_OK_AND_ASSIGN(auto lock, fs_.LockFile("/d/LOCK"));
  auto second = fs_.LockFile("/d/LOCK");
  ASSERT_FALSE(second.has_value());
  EXPECT_TRUE(second.error().IsBusy());
  lock.reset();
  EXPECT_TRUE(fs_.LockFile("/d/LOCK").has_value());
}

TEST_F(MemFsTest, GetChildrenListsNames) {
  WriteFile("/d/x.log", "", false, false);
  WriteFile("/d/y.sst", "", false, false);
  ASSERT_OK_AND_ASSIGN(auto names, fs_.GetChildren("/d"));
  std::ranges::sort(names);
  EXPECT_EQ(names, (std::vector<std::string>{"x.log", "y.sst"}));
}

TEST_F(MemFsTest, RandomAccessReadsAreBounded) {
  WriteFile("/d/a", "0123456789", false, false);
  ASSERT_OK_AND_ASSIGN(auto f, fs_.NewRandomAccessFile("/d/a"));
  char buf[4];
  ASSERT_OK_AND_ASSIGN(auto v, f->Read(8, buf));
  EXPECT_EQ(v, "89");
  ASSERT_OK_AND_ASSIGN(auto empty, f->Read(100, buf));
  EXPECT_TRUE(empty.empty());
}

}  // namespace
}  // namespace strata::testing
