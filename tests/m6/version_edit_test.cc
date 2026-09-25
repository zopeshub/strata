#include "db/version_edit.h"

#include <gtest/gtest.h>

#include "db/filename.h"
#include "support/mem_fs.h"
#include "support/test_util.h"
#include "util/coding.h"

namespace strata {
namespace {

using testing::Hex;
using testing::MemFileSystem;

// Tag numbers from docs/design.md §5.
constexpr std::uint32_t kTagDeletedFile = 6;
constexpr std::uint32_t kTagNewFile = 7;

VersionEdit RoundTrip(const VersionEdit& edit) {
  std::string encoded;
  edit.EncodeTo(encoded);
  auto decoded = VersionEdit::DecodeFrom(encoded);
  EXPECT_TRUE(decoded.has_value()) << decoded.error().ToString();
  if (!decoded) return {};
  // Re-encoding must be byte-identical (deterministic encoding).
  std::string again;
  decoded->EncodeTo(again);
  EXPECT_EQ(Hex(encoded), Hex(again));
  return *decoded;
}

FileMetaData File(std::uint64_t number, std::string_view smallest, std::string_view largest) {
  FileMetaData f;
  f.number = number;
  f.file_size = number * 1000 + 17;
  f.smallest = MakeInternalKey(smallest, 1000 + number, ValueType::kValue);
  f.largest = MakeInternalKey(largest, number, ValueType::kDeletion);
  return f;
}

TEST(VersionEditTest, EmptyRoundTrip) {
  VersionEdit empty;
  std::string encoded;
  empty.EncodeTo(encoded);
  EXPECT_TRUE(encoded.empty()) << "absent optional fields must not be encoded";
  EXPECT_EQ(RoundTrip(empty), empty);
}

TEST(VersionEditTest, FullRoundTrip) {
  VersionEdit edit;
  edit.SetComparatorName("strata.BytewiseComparator");
  edit.SetLogNumber(42);
  edit.SetNextFileNumber(1ull << 40);
  edit.SetLastSequence(kMaxSequenceNumber);
  for (int level = 0; level < kNumLevels; level++) {
    edit.AddFile(level, File(static_cast<std::uint64_t>(100 + level), "a" + std::to_string(level),
                             "z" + std::to_string(level)));
    edit.RemoveFile(level, static_cast<std::uint64_t>(level) * 3 + 1);
  }
  edit.AddFile(3, File(7, std::string("\x00\xff", 2), std::string(300, 'q')));
  EXPECT_EQ(RoundTrip(edit), edit);
}

TEST(VersionEditTest, OnlySomeFields) {
  VersionEdit edit;
  edit.SetLogNumber(9);
  edit.RemoveFile(2, 5);
  const VersionEdit got = RoundTrip(edit);
  EXPECT_EQ(got, edit);
  EXPECT_FALSE(got.comparator_name().has_value());
  EXPECT_FALSE(got.next_file_number().has_value());
  EXPECT_EQ(got.log_number(), 9u);
}

TEST(VersionEditTest, EveryStrictPrefixOfAFieldIsCorruption) {
  VersionEdit edit;
  edit.AddFile(1, File(12, "apple", "banana"));
  std::string encoded;
  edit.EncodeTo(encoded);
  ASSERT_FALSE(encoded.empty());
  for (std::size_t len = 1; len < encoded.size(); len++) {
    auto r = VersionEdit::DecodeFrom(std::string_view(encoded).substr(0, len));
    ASSERT_FALSE(r.has_value()) << "prefix of length " << len << " decoded";
    EXPECT_TRUE(r.error().IsCorruption());
  }
}

TEST(VersionEditTest, UnknownTagIsCorruption) {
  std::string bad;
  PutVarint32(bad, 99);
  PutVarint64(bad, 1);
  auto r = VersionEdit::DecodeFrom(bad);
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsCorruption());
}

TEST(VersionEditTest, BadLevelIsCorruption) {
  std::string bad;
  PutVarint32(bad, kTagDeletedFile);
  PutVarint32(bad, static_cast<std::uint32_t>(kNumLevels));
  PutVarint64(bad, 3);
  auto r = VersionEdit::DecodeFrom(bad);
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsCorruption());

  std::string bad2;
  PutVarint32(bad2, kTagNewFile);
  PutVarint32(bad2, 1000);
  auto r2 = VersionEdit::DecodeFrom(bad2);
  ASSERT_FALSE(r2.has_value());
  EXPECT_TRUE(r2.error().IsCorruption());
}

TEST(VersionEditTest, TagsMatchDesignDoc) {
  VersionEdit edit;
  edit.RemoveFile(2, 5);
  std::string encoded;
  edit.EncodeTo(encoded);
  EXPECT_EQ(Hex(encoded), "060205") << "tag 6, varint32 level, varint64 number";
}

// ---------------------------------------------------------------------------
// SetCurrentFile: the commit point.

class SetCurrentFileTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_OK(fs_.CreateDir(dir_)); }

  std::string Current() {
    auto s = fs_.ReadFileToString(CurrentFileName(dir_));
    return s ? *s : "<missing>";
  }

  int NonCurrentFiles() {
    auto children = fs_.GetChildren(dir_);
    EXPECT_TRUE(children.has_value());
    int n = 0;
    for (const auto& c : *children) n += (c != "CURRENT");
    return n;
  }

  MemFileSystem fs_;
  Path dir_ = "/db";
};

TEST_F(SetCurrentFileTest, WritesManifestNameWithNewline) {
  ASSERT_OK(SetCurrentFile(fs_, dir_, 5));
  EXPECT_EQ(Current(), "MANIFEST-000005\n");
  EXPECT_EQ(NonCurrentFiles(), 0) << "the temp file must be gone";
}

TEST_F(SetCurrentFileTest, DurableOnReturn) {
  ASSERT_OK(SetCurrentFile(fs_, dir_, 5));
  fs_.SimulatePowerLoss();
  EXPECT_EQ(Current(), "MANIFEST-000005\n")
      << "missing a file fsync (-> empty CURRENT) or the directory fsync (-> CURRENT missing)?";
  ASSERT_OK(SetCurrentFile(fs_, dir_, 9));
  fs_.SimulatePowerLoss(MemFileSystem::Unsynced::kKeepRandomPrefix, 3);
  EXPECT_EQ(Current(), "MANIFEST-000009\n");
}

TEST_F(SetCurrentFileTest, FailedSyncLeavesOldCurrentIntact) {
  ASSERT_OK(SetCurrentFile(fs_, dir_, 5));
  fs_.FailNextSyncs(1);
  Status s = SetCurrentFile(fs_, dir_, 6);
  EXPECT_FALSE(s.ok()) << "the sync failed, so the commit must fail";
  EXPECT_EQ(Current(), "MANIFEST-000005\n")
      << "CURRENT must never be modified in place: a reader (or crash) at any moment "
         "has to see a complete old or complete new value";
  EXPECT_EQ(NonCurrentFiles(), 0) << "clean up the temp file on failure";
  fs_.SimulatePowerLoss();
  EXPECT_EQ(Current(), "MANIFEST-000005\n");
}

TEST_F(SetCurrentFileTest, UsesADirectorySync) {
  const auto before = fs_.dir_sync_count();
  ASSERT_OK(SetCurrentFile(fs_, dir_, 5));
  EXPECT_GT(fs_.dir_sync_count(), before);
}

}  // namespace
}  // namespace strata
