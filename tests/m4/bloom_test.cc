#include "table/bloom.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "util/coding.h"

namespace strata {
namespace {

std::string Key(int i) {
  std::string s;
  PutFixed32(s, static_cast<std::uint32_t>(i));
  return s;
}

class BloomTest : public ::testing::Test {
 protected:
  void Build(const std::vector<std::string>& keys) {
    std::vector<std::string_view> views(keys.begin(), keys.end());
    filter_.clear();
    policy_.CreateFilter(views, filter_);
  }
  bool Matches(std::string_view key) const { return policy_.KeyMayMatch(key, filter_); }

  double FalsePositiveRate() const {
    int hits = 0;
    for (int i = 0; i < 10000; i++) {
      if (Matches(Key(i + 1000000000))) hits++;
    }
    return hits / 10000.0;
  }

  BloomFilterPolicy policy_{10};
  std::string filter_;
};

TEST_F(BloomTest, EmptyFilter) {
  Build({});
  EXPECT_FALSE(Matches("hello"));
  EXPECT_FALSE(Matches("world"));
}

TEST_F(BloomTest, Small) {
  Build({"hello", "world"});
  EXPECT_TRUE(Matches("hello"));
  EXPECT_TRUE(Matches("world"));
  EXPECT_FALSE(Matches("x"));
  EXPECT_FALSE(Matches("foo"));
}

TEST_F(BloomTest, MalformedFilterSaysMaybe) {
  EXPECT_TRUE(policy_.KeyMayMatch("x", "")) << "no filter: must not claim absence";
  EXPECT_TRUE(policy_.KeyMayMatch("x", std::string_view("\0", 1)));
  std::string reserved(16, '\0');
  reserved.push_back(static_cast<char>(31));  // k > 30 is reserved for future encodings
  EXPECT_TRUE(policy_.KeyMayMatch("x", reserved));
}

int NextLength(int length) {
  if (length < 10) return length + 1;
  if (length < 100) return length + 10;
  if (length < 1000) return length + 100;
  return length + 1000;
}

TEST_F(BloomTest, VaryingLengths) {
  int mediocre = 0, good = 0;
  for (int length = 1; length <= 10000; length = NextLength(length)) {
    std::vector<std::string> keys;
    for (int i = 0; i < length; i++) keys.push_back(Key(i));
    Build(keys);
    EXPECT_LE(filter_.size(), static_cast<std::size_t>((length * 10 / 8) + 40)) << length;
    for (int i = 0; i < length; i++) {
      ASSERT_TRUE(Matches(Key(i))) << "false negative: length " << length << " key " << i;
    }
    const double rate = FalsePositiveRate();
    EXPECT_LE(rate, 0.02) << "length " << length;
    if (rate > 0.0125) mediocre++;
    else good++;
  }
  EXPECT_LE(mediocre, good / 5) << "too many mediocre filters";
}

TEST(BloomPolicyTest, AppendsToDst) {
  BloomFilterPolicy policy(10);
  std::string dst = "prefix";
  std::vector<std::string_view> keys = {"a", "b"};
  policy.CreateFilter(keys, dst);
  ASSERT_GT(dst.size(), 6u);
  EXPECT_EQ(dst.substr(0, 6), "prefix");
  const std::string_view filter = std::string_view(dst).substr(6);
  EXPECT_TRUE(policy.KeyMayMatch("a", filter));
  EXPECT_TRUE(policy.KeyMayMatch("b", filter));
}

TEST(BloomPolicyTest, ReaderUsesStoredK) {
  // A filter built with 20 bits/key must be readable by a policy configured
  // with 5: k travels with the filter.
  BloomFilterPolicy writer(20), reader(5);
  std::vector<std::string> keys;
  for (int i = 0; i < 1000; i++) keys.push_back(Key(i));
  std::vector<std::string_view> views(keys.begin(), keys.end());
  std::string filter;
  writer.CreateFilter(views, filter);
  for (const auto& k : keys) ASSERT_TRUE(reader.KeyMayMatch(k, filter));
}

}  // namespace
}  // namespace strata
