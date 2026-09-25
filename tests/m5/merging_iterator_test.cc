#include "table/merging_iterator.h"

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "support/test_util.h"

namespace strata {
namespace {

using testing::VectorIterator;
using KVs = std::vector<std::pair<std::string, std::string>>;

std::unique_ptr<Iterator> Vec(KVs kvs) {
  return std::make_unique<VectorIterator>(std::move(kvs), *BytewiseComparator());
}

std::vector<std::string> Keys(Iterator& it) {
  std::vector<std::string> out;
  for (; it.Valid(); it.Next()) out.emplace_back(it.key());
  return out;
}

TEST(MergingIteratorTest, NoChildren) {
  auto it = NewMergingIterator(*BytewiseComparator(), {});
  it->SeekToFirst();
  EXPECT_FALSE(it->Valid());
  EXPECT_OK(it->status());
}

TEST(MergingIteratorTest, SingleChildPassesThrough) {
  std::vector<std::unique_ptr<Iterator>> children;
  children.push_back(Vec({{"a", "1"}, {"b", "2"}}));
  auto it = NewMergingIterator(*BytewiseComparator(), std::move(children));
  it->SeekToFirst();
  EXPECT_EQ(testing::Drain(*it), (KVs{{"a", "1"}, {"b", "2"}}));
}

TEST(MergingIteratorTest, InterleavedChildren) {
  std::vector<std::unique_ptr<Iterator>> children;
  children.push_back(Vec({{"a", "1"}, {"d", "1"}, {"g", "1"}}));
  children.push_back(Vec({{"b", "2"}, {"e", "2"}}));
  children.push_back(Vec({}));
  children.push_back(Vec({{"c", "3"}, {"f", "3"}, {"h", "3"}}));
  auto it = NewMergingIterator(*BytewiseComparator(), std::move(children));
  it->SeekToFirst();
  EXPECT_EQ(Keys(*it), (std::vector<std::string>{"a", "b", "c", "d", "e", "f", "g", "h"}));
  EXPECT_OK(it->status());
}

TEST(MergingIteratorTest, EqualKeysAreAllYielded) {
  std::vector<std::unique_ptr<Iterator>> children;
  children.push_back(Vec({{"k", "from-0"}}));
  children.push_back(Vec({{"k", "from-1"}}));
  auto it = NewMergingIterator(*BytewiseComparator(), std::move(children));
  it->SeekToFirst();
  auto got = testing::Drain(*it);
  ASSERT_EQ(got.size(), 2u);
  EXPECT_EQ(got[0].first, "k");
  EXPECT_EQ(got[1].first, "k");
}

TEST(MergingIteratorTest, Seek) {
  std::vector<std::unique_ptr<Iterator>> children;
  children.push_back(Vec({{"a", ""}, {"m", ""}, {"z", ""}}));
  children.push_back(Vec({{"c", ""}, {"n", ""}}));
  auto it = NewMergingIterator(*BytewiseComparator(), std::move(children));
  it->Seek("m");
  EXPECT_EQ(Keys(*it), (std::vector<std::string>{"m", "n", "z"}));
  it->Seek("b");
  EXPECT_EQ(Keys(*it), (std::vector<std::string>{"c", "m", "n", "z"}));
  it->Seek("zz");
  EXPECT_FALSE(it->Valid());
}

TEST(MergingIteratorTest, ChildErrorStopsTheMerge) {
  auto bad =
      std::make_unique<VectorIterator>(KVs{{"b", ""}, {"d", ""}, {"f", ""}}, *BytewiseComparator());
  bad->InjectErrorAt(1, Status::IOError("disk on fire"));
  std::vector<std::unique_ptr<Iterator>> children;
  children.push_back(Vec({{"a", ""}, {"c", ""}, {"e", ""}, {"g", ""}}));
  children.push_back(std::move(bad));
  auto it = NewMergingIterator(*BytewiseComparator(), std::move(children));
  it->SeekToFirst();
  std::vector<std::string> got = Keys(*it);
  EXPECT_TRUE(it->status().IsIOError()) << "the child's error must surface";
  EXPECT_LT(got.size(), 7u) << "must not silently yield keys past the failed child";
}

TEST(MergingIteratorTest, RandomizedAgainstStdMerge) {
  Random rnd(77);
  for (int trial = 0; trial < 50; trial++) {
    const int n = 1 + static_cast<int>(rnd.Uniform(10));
    std::vector<std::unique_ptr<Iterator>> children;
    std::vector<std::string> all;
    for (int c = 0; c < n; c++) {
      std::vector<std::string> keys;
      const int m = static_cast<int>(rnd.Uniform(200));
      for (int i = 0; i < m; i++)
        keys.push_back(testing::NumberedKey(rnd.Uniform(100000), 6) + "/" + std::to_string(c));
      std::ranges::sort(keys);
      keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
      KVs kvs;
      for (auto& k : keys) kvs.emplace_back(k, "");
      all.insert(all.end(), keys.begin(), keys.end());
      children.push_back(Vec(std::move(kvs)));
    }
    std::ranges::sort(all);
    auto it = NewMergingIterator(*BytewiseComparator(), std::move(children));
    it->SeekToFirst();
    ASSERT_EQ(Keys(*it), all) << "trial " << trial;
  }
}

}  // namespace
}  // namespace strata
