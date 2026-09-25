#include "db/dbformat.h"

#include <gtest/gtest.h>

#include "util/coding.h"

namespace strata {
namespace {

std::string IKey(std::string_view user_key, SequenceNumber seq, ValueType t) {
  return MakeInternalKey(user_key, seq, t);
}

TEST(DbFormatTest, PackSequenceAndType) {
  EXPECT_EQ(PackSequenceAndType(0, ValueType::kDeletion), 0u);
  EXPECT_EQ(PackSequenceAndType(1, ValueType::kValue), (1u << 8) | 1u);
  EXPECT_EQ(PackSequenceAndType(kMaxSequenceNumber, ValueType::kValue),
            (kMaxSequenceNumber << 8) | 1u);
}

TEST(DbFormatTest, EncodingIsUserKeyPlusFixed64Trailer) {
  std::string expected = "foo";
  PutFixed64(expected, (std::uint64_t{100} << 8) | 1u);
  EXPECT_EQ(IKey("foo", 100, ValueType::kValue), expected);

  std::string appended = "prefix:";
  AppendInternalKey(appended, {"foo", 100, ValueType::kValue});
  EXPECT_EQ(appended, "prefix:" + expected);
}

TEST(DbFormatTest, ParseRoundTrip) {
  const std::string keys[] = {"", "k", "hello", "longggggggggggggggggggggg",
                              std::string("\0\xff", 2)};
  const SequenceNumber seqs[] = {
      0, 1, 2, 10, 1ull << 8, (1ull << 8) + 1, 1ull << 32, kMaxSequenceNumber};
  for (const auto& k : keys) {
    for (auto s : seqs) {
      for (auto t : {ValueType::kValue, ValueType::kDeletion}) {
        const std::string ikey = IKey(k, s, t);
        auto parsed = ParseInternalKey(ikey);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(parsed->user_key, k);
        EXPECT_EQ(parsed->sequence, s);
        EXPECT_EQ(parsed->type, t);
        EXPECT_EQ(ExtractUserKey(ikey), k);
      }
    }
  }
}

TEST(DbFormatTest, ParseRejectsGarbage) {
  EXPECT_FALSE(ParseInternalKey("").has_value());
  EXPECT_FALSE(ParseInternalKey("1234567").has_value()) << "shorter than the trailer";
  std::string bad_type = "foo";
  PutFixed64(bad_type, (std::uint64_t{5} << 8) | 0x7f);
  EXPECT_FALSE(ParseInternalKey(bad_type).has_value()) << "unknown value type";
}

TEST(DbFormatTest, ParsedViewPointsIntoInput) {
  const std::string ikey = IKey("user", 5, ValueType::kValue);
  auto parsed = ParseInternalKey(ikey);
  ASSERT_TRUE(parsed.has_value());
  EXPECT_EQ(parsed->user_key.data(), ikey.data());
}

class InternalKeyComparatorTest : public ::testing::Test {
 protected:
  int Cmp(const std::string& a, const std::string& b) { return icmp_.Compare(a, b); }
  InternalKeyComparator icmp_;
};

TEST_F(InternalKeyComparatorTest, UserKeyAscending) {
  EXPECT_LT(Cmp(IKey("a", 1, ValueType::kValue), IKey("b", 100, ValueType::kValue)), 0);
  EXPECT_LT(Cmp(IKey("a", 100, ValueType::kValue), IKey("ab", 1, ValueType::kValue)), 0)
      << "a user key sorts before its extensions, regardless of trailer bytes";
  EXPECT_GT(Cmp(IKey("\xff", 1, ValueType::kValue), IKey("a", 1, ValueType::kValue)), 0)
      << "bytes compare as unsigned";
}

TEST_F(InternalKeyComparatorTest, SequenceDescending) {
  EXPECT_LT(Cmp(IKey("a", 100, ValueType::kValue), IKey("a", 1, ValueType::kValue)), 0)
      << "newer (higher sequence) versions sort first";
  EXPECT_GT(Cmp(IKey("a", 1, ValueType::kValue), IKey("a", 2, ValueType::kDeletion)), 0);
}

TEST_F(InternalKeyComparatorTest, TypeDescendingAtEqualSequence) {
  EXPECT_LT(Cmp(IKey("a", 5, ValueType::kValue), IKey("a", 5, ValueType::kDeletion)), 0);
}

TEST_F(InternalKeyComparatorTest, Equal) {
  EXPECT_EQ(Cmp(IKey("a", 5, ValueType::kValue), IKey("a", 5, ValueType::kValue)), 0);
}

TEST(LookupKeyTest, Accessors) {
  LookupKey lk("user-key", 42);
  EXPECT_EQ(lk.user_key(), "user-key");
  EXPECT_EQ(lk.internal_key(), IKey("user-key", 42, kValueTypeForSeek));
}

TEST(LookupKeyTest, LongKey) {
  const std::string big(10000, 'x');
  LookupKey lk(big, 7);
  EXPECT_EQ(lk.user_key(), big);
  EXPECT_EQ(lk.internal_key(), IKey(big, 7, kValueTypeForSeek));
}

TEST(LookupKeyTest, OrdersBetweenVersions) {
  InternalKeyComparator icmp;
  LookupKey lk("k", 8);
  EXPECT_GT(icmp.Compare(lk.internal_key(), IKey("k", 9, ValueType::kValue)), 0);
  EXPECT_LE(icmp.Compare(lk.internal_key(), IKey("k", 8, ValueType::kValue)), 0);
  EXPECT_LT(icmp.Compare(lk.internal_key(), IKey("k", 7, ValueType::kValue)), 0);
}

}  // namespace
}  // namespace strata
