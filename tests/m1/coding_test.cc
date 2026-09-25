#include "util/coding.h"

#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace strata {
namespace {

std::string Bytes(std::initializer_list<unsigned char> bytes) {
  std::string s;
  for (unsigned char b : bytes) s.push_back(static_cast<char>(b));
  return s;
}

TEST(CodingTest, Fixed32IsLittleEndianBytes) {
  char buf[4];
  EncodeFixed32(buf, 0x04030201u);
  EXPECT_EQ(std::string(buf, 4), Bytes({0x01, 0x02, 0x03, 0x04}));
  EXPECT_EQ(DecodeFixed32(buf), 0x04030201u);
}

TEST(CodingTest, Fixed64IsLittleEndianBytes) {
  char buf[8];
  EncodeFixed64(buf, 0x0807060504030201ull);
  EXPECT_EQ(std::string(buf, 8), Bytes({0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}));
  EXPECT_EQ(DecodeFixed64(buf), 0x0807060504030201ull);
}

TEST(CodingTest, Fixed32HighBitsSurvive) {
  // Catches sign-extension bugs from treating bytes as (signed) char.
  char buf[4];
  EncodeFixed32(buf, 0xfffefdfcu);
  EXPECT_EQ(DecodeFixed32(buf), 0xfffefdfcu);
}

TEST(CodingTest, PutFixedRoundTrip) {
  std::string s;
  for (std::uint32_t v = 0; v < 100000; v++) PutFixed32(s, v);
  std::string_view in = s;
  for (std::uint32_t v = 0; v < 100000; v++) {
    auto got = GetFixed32(in);
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(*got, v);
  }
  EXPECT_TRUE(in.empty());
}

TEST(CodingTest, PutFixed64RoundTrip) {
  std::string s;
  for (int power = 0; power <= 63; power++) {
    std::uint64_t v = std::uint64_t{1} << power;
    PutFixed64(s, v - 1);
    PutFixed64(s, v + 0);
    PutFixed64(s, v + 1);
  }
  std::string_view in = s;
  for (int power = 0; power <= 63; power++) {
    std::uint64_t v = std::uint64_t{1} << power;
    EXPECT_EQ(GetFixed64(in), v - 1);
    EXPECT_EQ(GetFixed64(in), v + 0);
    EXPECT_EQ(GetFixed64(in), v + 1);
  }
  EXPECT_TRUE(in.empty());
}

TEST(CodingTest, GetFixedRejectsShortInput) {
  std::string_view three("abc", 3);
  EXPECT_FALSE(GetFixed32(three).has_value());
  std::string_view seven("abcdefg", 7);
  EXPECT_FALSE(GetFixed64(seven).has_value());
}

TEST(CodingTest, Varint32KnownEncodings) {
  struct Case {
    std::uint32_t value;
    std::string encoded;
  };
  const std::vector<Case> cases = {
      {0, Bytes({0x00})},
      {1, Bytes({0x01})},
      {127, Bytes({0x7f})},
      {128, Bytes({0x80, 0x01})},
      {300, Bytes({0xac, 0x02})},
      {16384, Bytes({0x80, 0x80, 0x01})},
      {0xffffffffu, Bytes({0xff, 0xff, 0xff, 0xff, 0x0f})},
  };
  for (const auto& c : cases) {
    std::string s;
    PutVarint32(s, c.value);
    EXPECT_EQ(s, c.encoded) << "value " << c.value;
    EXPECT_EQ(VarintLength(c.value), static_cast<int>(c.encoded.size()));
    std::string_view in = s;
    EXPECT_EQ(GetVarint32(in), c.value);
    EXPECT_TRUE(in.empty());
  }
}

TEST(CodingTest, Varint64MaxIsTenBytes) {
  std::string s;
  PutVarint64(s, std::numeric_limits<std::uint64_t>::max());
  EXPECT_EQ(s, Bytes({0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01}));
  EXPECT_EQ(VarintLength(std::numeric_limits<std::uint64_t>::max()), kMaxVarint64Bytes);
}

TEST(CodingTest, Varint32RoundTrip) {
  std::string s;
  for (std::uint32_t i = 0; i < (32 * 32); i++) {
    std::uint32_t v = (i / 32) << (i % 32);
    PutVarint32(s, v);
  }
  std::string_view in = s;
  for (std::uint32_t i = 0; i < (32 * 32); i++) {
    std::uint32_t expected = (i / 32) << (i % 32);
    const std::size_t before = in.size();
    auto actual = GetVarint32(in);
    ASSERT_TRUE(actual.has_value()) << i;
    ASSERT_EQ(*actual, expected);
    ASSERT_EQ(static_cast<int>(before - in.size()), VarintLength(expected));
  }
  EXPECT_TRUE(in.empty());
}

TEST(CodingTest, Varint64RoundTrip) {
  std::vector<std::uint64_t> values = {0, 100, ~std::uint64_t{0}, ~std::uint64_t{0} - 1};
  for (int k = 0; k < 64; k++) {
    const std::uint64_t power = std::uint64_t{1} << k;
    values.push_back(power);
    values.push_back(power - 1);
    values.push_back(power + 1);
  }
  std::string s;
  for (auto v : values) PutVarint64(s, v);
  std::string_view in = s;
  for (auto expected : values) {
    const std::size_t before = in.size();
    auto actual = GetVarint64(in);
    ASSERT_TRUE(actual.has_value());
    ASSERT_EQ(*actual, expected);
    ASSERT_EQ(static_cast<int>(before - in.size()), VarintLength(expected));
  }
  EXPECT_TRUE(in.empty());
}

TEST(CodingTest, EncodeVarintReturnsEnd) {
  char buf[kMaxVarint64Bytes];
  EXPECT_EQ(EncodeVarint32(buf, 300) - buf, 2);
  EXPECT_EQ(EncodeVarint64(buf, std::numeric_limits<std::uint64_t>::max()) - buf, 10);
}

TEST(CodingTest, Varint32RejectsTruncation) {
  std::string s;
  PutVarint32(s, 0xffffffffu);
  for (std::size_t len = 0; len < s.size(); len++) {
    std::string_view in(s.data(), len);
    EXPECT_FALSE(GetVarint32(in).has_value()) << "prefix of length " << len;
  }
}

TEST(CodingTest, Varint64RejectsTruncation) {
  std::string s;
  PutVarint64(s, std::uint64_t{1} << 63);
  for (std::size_t len = 0; len < s.size(); len++) {
    std::string_view in(s.data(), len);
    EXPECT_FALSE(GetVarint64(in).has_value()) << "prefix of length " << len;
  }
}

TEST(CodingTest, Varint32RejectsOverlongEncoding) {
  // Six bytes can never be a uint32.
  std::string six = Bytes({0x81, 0x80, 0x80, 0x80, 0x80, 0x00});
  std::string_view in6 = six;
  EXPECT_FALSE(GetVarint32(in6).has_value());
  // Fifth byte contributes bits 28..34: anything above 0x0f overflows 32 bits.
  std::string overflow = Bytes({0x80, 0x80, 0x80, 0x80, 0x10});
  std::string_view in5 = overflow;
  EXPECT_FALSE(GetVarint32(in5).has_value());
}

TEST(CodingTest, Varint64RejectsOverlongEncoding) {
  std::string eleven = Bytes({0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01});
  std::string_view in = eleven;
  EXPECT_FALSE(GetVarint64(in).has_value());
  // Tenth byte may only contribute bit 63.
  std::string overflow = Bytes({0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x02});
  std::string_view in10 = overflow;
  EXPECT_FALSE(GetVarint64(in10).has_value());
}

TEST(CodingTest, LengthPrefixedRoundTrip) {
  std::string s;
  PutLengthPrefixed(s, "");
  PutLengthPrefixed(s, "foo");
  PutLengthPrefixed(s, "bar");
  PutLengthPrefixed(s, std::string(200, 'x'));
  std::string_view in = s;
  EXPECT_EQ(GetLengthPrefixed(in), "");
  EXPECT_EQ(GetLengthPrefixed(in), "foo");
  EXPECT_EQ(GetLengthPrefixed(in), "bar");
  EXPECT_EQ(GetLengthPrefixed(in), std::string(200, 'x'));
  EXPECT_TRUE(in.empty());
}

TEST(CodingTest, LengthPrefixedPointsIntoInput) {
  std::string s;
  PutLengthPrefixed(s, "hello");
  std::string_view in = s;
  auto v = GetLengthPrefixed(in);
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(v->data(), s.data() + 1) << "must be a view into the input, not a copy";
}

TEST(CodingTest, LengthPrefixedRejectsTruncatedPayload) {
  std::string s;
  PutLengthPrefixed(s, "hello");
  s.pop_back();
  std::string_view in = s;
  EXPECT_FALSE(GetLengthPrefixed(in).has_value());
}

TEST(CodingTest, MixedSequentialDecode) {
  std::string s;
  PutVarint32(s, 7);
  PutFixed64(s, 99);
  PutLengthPrefixed(s, "key");
  PutVarint64(s, 1ull << 40);
  PutFixed32(s, 5);
  std::string_view in = s;
  EXPECT_EQ(GetVarint32(in), 7u);
  EXPECT_EQ(GetFixed64(in), 99u);
  EXPECT_EQ(GetLengthPrefixed(in), "key");
  EXPECT_EQ(GetVarint64(in), 1ull << 40);
  EXPECT_EQ(GetFixed32(in), 5u);
  EXPECT_TRUE(in.empty());
}

}  // namespace
}  // namespace strata
