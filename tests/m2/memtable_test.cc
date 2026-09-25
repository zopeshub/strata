// MemTable behaviour. These same tests must pass for your M2 (std::map)
// memtable AND your M3 (arena + skiplist) memtable: the interface is the
// contract, the representation is yours.

#include "db/memtable.h"

#include <algorithm>
#include <map>
#include <set>

#include <gtest/gtest.h>

#include "support/test_util.h"

namespace strata {
namespace {

std::string ValueOf(const LookupResult& r) {
  if (std::holds_alternative<KeyAbsent>(r)) return "ABSENT";
  if (std::holds_alternative<KeyDeleted>(r)) return "DELETED";
  return std::get<std::string>(r);
}

class MemTableTest : public ::testing::Test {
 protected:
  std::string Get(std::string_view key, SequenceNumber seq) {
    return ValueOf(mem_.Get(LookupKey(key, seq)));
  }

  InternalKeyComparator icmp_;
  MemTable mem_{icmp_};
};

TEST_F(MemTableTest, EmptyIsAbsent) {
  EXPECT_EQ(Get("a", kMaxSequenceNumber), "ABSENT");
  auto it = mem_.NewIterator();
  it->SeekToFirst();
  EXPECT_FALSE(it->Valid());
  EXPECT_OK(it->status());
}

TEST_F(MemTableTest, AddAndGet) {
  mem_.Add(1, ValueType::kValue, "a", "va");
  mem_.Add(2, ValueType::kValue, "b", "vb");
  EXPECT_EQ(Get("a", 10), "va");
  EXPECT_EQ(Get("b", 10), "vb");
  EXPECT_EQ(Get("c", 10), "ABSENT");
}

TEST_F(MemTableTest, VersionsAndSnapshots) {
  mem_.Add(1, ValueType::kValue, "k", "v1");
  mem_.Add(5, ValueType::kValue, "k", "v5");
  mem_.Add(9, ValueType::kDeletion, "k", "");
  EXPECT_EQ(Get("k", 0), "ABSENT") << "nothing visible before seq 1";
  EXPECT_EQ(Get("k", 1), "v1");
  EXPECT_EQ(Get("k", 4), "v1");
  EXPECT_EQ(Get("k", 5), "v5");
  EXPECT_EQ(Get("k", 8), "v5");
  EXPECT_EQ(Get("k", 9), "DELETED");
  EXPECT_EQ(Get("k", kMaxSequenceNumber), "DELETED");
}

TEST_F(MemTableTest, PrefixKeysAreDistinct) {
  mem_.Add(1, ValueType::kValue, "a", "short");
  mem_.Add(2, ValueType::kValue, "ab", "long");
  mem_.Add(3, ValueType::kValue, "", "empty");
  EXPECT_EQ(Get("a", 10), "short");
  EXPECT_EQ(Get("ab", 10), "long");
  EXPECT_EQ(Get("", 10), "empty");
  EXPECT_EQ(Get("abc", 10), "ABSENT");
}

TEST_F(MemTableTest, BinaryKeysAndValues) {
  const std::string k1("\0\0", 2), k2("\0\xff", 2), v("\xff\0\x01", 3);
  mem_.Add(1, ValueType::kValue, k1, v);
  mem_.Add(2, ValueType::kValue, k2, v);
  EXPECT_EQ(Get(k1, 10), v);
  EXPECT_EQ(Get(k2, 10), v);
  EXPECT_EQ(Get(std::string("\0", 1), 10), "ABSENT");
}

TEST_F(MemTableTest, IteratorYieldsInternalKeyOrder) {
  mem_.Add(3, ValueType::kValue, "b", "b3");
  mem_.Add(1, ValueType::kValue, "a", "a1");
  mem_.Add(7, ValueType::kDeletion, "a", "");
  mem_.Add(4, ValueType::kValue, "a", "a4");
  auto it = mem_.NewIterator();
  it->SeekToFirst();
  std::vector<std::string> got;
  for (; it->Valid(); it->Next()) {
    auto parsed = ParseInternalKey(it->key());
    ASSERT_TRUE(parsed.has_value());
    got.push_back(std::string(parsed->user_key) + "@" + std::to_string(parsed->sequence) + "=" +
                  std::string(it->value()));
  }
  EXPECT_OK(it->status());
  EXPECT_EQ(got, (std::vector<std::string>{"a@7=", "a@4=a4", "a@1=a1", "b@3=b3"}));
}

TEST_F(MemTableTest, IteratorSeek) {
  for (int i = 0; i < 100; i += 2) {
    mem_.Add(static_cast<SequenceNumber>(i + 1), ValueType::kValue, testing::NumberedKey(i, 4),
             "v");
  }
  auto it = mem_.NewIterator();
  it->Seek(LookupKey(testing::NumberedKey(41, 4), kMaxSequenceNumber).internal_key());
  ASSERT_TRUE(it->Valid());
  EXPECT_EQ(ExtractUserKey(it->key()), testing::NumberedKey(42, 4));
  it->Seek(LookupKey(testing::NumberedKey(99, 4), kMaxSequenceNumber).internal_key());
  EXPECT_FALSE(it->Valid());
}

TEST_F(MemTableTest, RandomizedAgainstModel) {
  Random rnd(1234);
  std::map<std::string, std::vector<std::pair<SequenceNumber, std::string>>> model;
  SequenceNumber seq = 0;
  for (int i = 0; i < 5000; i++) {
    const std::string key = testing::NumberedKey(rnd.Uniform(300), 5);
    ++seq;
    if (rnd.OneIn(5)) {
      mem_.Add(seq, ValueType::kDeletion, key, "");
      model[key].emplace_back(seq, "DELETED");
    } else {
      const std::string v = testing::RandomString(rnd, rnd.Uniform(50));
      mem_.Add(seq, ValueType::kValue, key, v);
      model[key].emplace_back(seq, v);
    }
  }
  for (int probe = 0; probe < 2000; probe++) {
    const std::string key = testing::NumberedKey(rnd.Uniform(320), 5);
    const SequenceNumber snap = rnd.Uniform(seq + 2);
    std::string expected = "ABSENT";
    for (const auto& [s, v] : model[key]) {
      if (s <= snap) expected = v;
    }
    ASSERT_EQ(Get(key, snap), expected) << key << " @" << snap;
  }
  // Full iteration is sorted and complete.
  auto it = mem_.NewIterator();
  std::size_t count = 0;
  std::string prev;
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    if (count > 0) {
      ASSERT_LT(icmp_.Compare(prev, it->key()), 0);
    }
    prev = std::string(it->key());
    ++count;
  }
  EXPECT_EQ(count, 5000u);
}

TEST_F(MemTableTest, MemoryUsageGrows) {
  const std::size_t before = mem_.ApproximateMemoryUsage();
  const std::string value(100, 'v');
  for (int i = 0; i < 1000; i++) {
    mem_.Add(static_cast<SequenceNumber>(i + 1), ValueType::kValue, testing::NumberedKey(i), value);
  }
  const std::size_t after = mem_.ApproximateMemoryUsage();
  EXPECT_GE(after - before, 1000u * (100 + 17)) << "must count at least the key+value bytes";
  EXPECT_LE(after - before, 1000u * (100 + 17) * 4) << "overhead estimate is implausibly high";
}

TEST_F(MemTableTest, LargeValues) {
  const std::string big(1 << 20, 'x');
  mem_.Add(1, ValueType::kValue, "big", big);
  EXPECT_EQ(Get("big", 1), big);
}

}  // namespace
}  // namespace strata
