#include "table/block.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "support/test_util.h"
#include "table/block_builder.h"
#include "util/coding.h"

namespace strata {
namespace {

using KVs = std::vector<std::pair<std::string, std::string>>;

KVs MakeEntries(int n, Random& rnd) {
  KVs kvs;
  for (int i = 0; i < n; i++) {
    // Shared prefixes of varying length exercise prefix compression.
    kvs.emplace_back(
        "user:" + std::to_string(1000000 + i * 3) + ":" + std::string(rnd.Uniform(5), 'x'),
        testing::RandomString(rnd, rnd.Uniform(40)));
  }
  return kvs;
}

std::string Build(const KVs& kvs, int restart_interval) {
  BlockBuilder b(restart_interval);
  for (const auto& [k, v] : kvs) b.Add(k, v);
  return std::string(b.Finish());
}

TEST(BlockTest, EmptyBlock) {
  BlockBuilder b(16);
  EXPECT_TRUE(b.empty());
  const std::string contents(b.Finish());
  EXPECT_EQ(contents.size(), 8u) << "one restart (offset 0) + num_restarts";
  Block block(contents);
  auto it = block.NewIterator(*BytewiseComparator());
  it->SeekToFirst();
  EXPECT_FALSE(it->Valid());
  EXPECT_OK(it->status());
  it->Seek("anything");
  EXPECT_FALSE(it->Valid());
}

class BlockRoundTripTest : public ::testing::TestWithParam<int> {};

TEST_P(BlockRoundTripTest, IterateAndSeek) {
  Random rnd(static_cast<std::uint64_t>(GetParam()));
  const KVs kvs = MakeEntries(1000, rnd);
  Block block(Build(kvs, GetParam()));
  auto it = block.NewIterator(*BytewiseComparator());

  it->SeekToFirst();
  EXPECT_EQ(testing::Drain(*it), kvs);

  for (std::size_t i = 0; i < kvs.size(); i += 13) {
    it->Seek(kvs[i].first);
    ASSERT_TRUE(it->Valid());
    EXPECT_EQ(it->key(), kvs[i].first);
    EXPECT_EQ(it->value(), kvs[i].second);
    // A target just after key i lands on key i+1.
    it->Seek(kvs[i].first + "\x01");
    if (i + 1 < kvs.size()) {
      ASSERT_TRUE(it->Valid());
      EXPECT_EQ(it->key(), kvs[i + 1].first);
    } else {
      EXPECT_FALSE(it->Valid());
    }
  }
  it->Seek("");
  ASSERT_TRUE(it->Valid());
  EXPECT_EQ(it->key(), kvs.front().first);
  it->Seek("zzzz");
  EXPECT_FALSE(it->Valid());
  EXPECT_OK(it->status());
}

INSTANTIATE_TEST_SUITE_P(RestartInterval, BlockRoundTripTest, ::testing::Values(1, 2, 16, 1000));

TEST(BlockTest, SizeEstimateIsExact) {
  Random rnd(5);
  const KVs kvs = MakeEntries(200, rnd);
  BlockBuilder b(16);
  for (const auto& [k, v] : kvs) b.Add(k, v);
  const std::size_t estimate = b.CurrentSizeEstimate();
  EXPECT_EQ(b.Finish().size(), estimate);
}

TEST(BlockTest, PrefixCompressionShrinksBlocks) {
  KVs kvs;
  for (int i = 0; i < 500; i++)
    kvs.emplace_back(std::string(100, 'p') + testing::NumberedKey(static_cast<std::uint64_t>(i)),
                     "v");
  const std::size_t compressed = Build(kvs, 16).size();
  const std::size_t uncompressed = Build(kvs, 1).size();
  EXPECT_LT(compressed * 3, uncompressed);
}

TEST(BlockTest, ResetReuses) {
  BlockBuilder b(4);
  b.Add("a", "1");
  b.Add("b", "2");
  const std::string first(b.Finish());
  b.Reset();
  EXPECT_TRUE(b.empty());
  b.Add("a", "1");
  b.Add("b", "2");
  EXPECT_EQ(std::string(b.Finish()), first);
}

TEST(BlockTest, BinaryKeysAndEmptyValues) {
  KVs kvs = {{std::string("\0", 1), ""}, {std::string("\0\0", 2), "x"}, {"\xff", ""}};
  Block block(Build(kvs, 2));
  auto it = block.NewIterator(*BytewiseComparator());
  it->SeekToFirst();
  EXPECT_EQ(testing::Drain(*it), kvs);
}

TEST(BlockTest, TooShortIsCorruption) {
  Block block(std::string("ab"));
  auto it = block.NewIterator(*BytewiseComparator());
  it->SeekToFirst();
  EXPECT_FALSE(it->Valid());
  EXPECT_TRUE(it->status().IsCorruption());
}

TEST(BlockTest, ImpossibleRestartCountIsCorruption) {
  std::string contents = Build({{"a", "1"}}, 16);
  EncodeFixed32(contents.data() + contents.size() - 4, 1u << 30);
  Block block(contents);
  auto it = block.NewIterator(*BytewiseComparator());
  it->SeekToFirst();
  EXPECT_FALSE(it->Valid());
  EXPECT_TRUE(it->status().IsCorruption());
}

TEST(BlockTest, EntryRunningOffTheEndIsCorruption) {
  std::string contents = Build({{"key", "value"}}, 16);
  contents[2] = '\x7f';  // value_len of the first entry: now runs into the restart array and beyond
  Block block(contents);
  auto it = block.NewIterator(*BytewiseComparator());
  it->SeekToFirst();
  EXPECT_FALSE(it->Valid());
  EXPECT_TRUE(it->status().IsCorruption()) << it->status().ToString();
}

TEST(BlockTest, RandomCorruptionNeverCrashes) {
  // Under the asan preset this proves you never read outside the block.
  Random rnd(99);
  const KVs kvs = MakeEntries(100, rnd);
  const std::string good = Build(kvs, 4);
  for (int trial = 0; trial < 2000; trial++) {
    std::string bad = good;
    const int flips = 1 + static_cast<int>(rnd.Uniform(4));
    for (int f = 0; f < flips; f++) {
      bad[rnd.Uniform(bad.size())] = static_cast<char>(rnd.Uniform(256));
    }
    if (rnd.OneIn(4)) bad.resize(rnd.Uniform(bad.size() + 1));
    Block block(bad);
    auto it = block.NewIterator(*BytewiseComparator());
    int steps = 0;
    for (it->SeekToFirst(); it->Valid() && steps < 10000; it->Next()) ++steps;
    it->Seek(kvs[rnd.Uniform(kvs.size())].first);
    ASSERT_LT(steps, 10000) << "iteration over a corrupt block must terminate";
  }
}

}  // namespace
}  // namespace strata
