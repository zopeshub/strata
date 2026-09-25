#include "util/crc32c.h"

#include <string>

#include <gtest/gtest.h>

namespace strata::crc32c {
namespace {

// Test vectors from RFC 3720 (iSCSI), section B.4.
TEST(Crc32cTest, StandardResults) {
  std::string buf(32, '\0');
  EXPECT_EQ(0x8a9136aau, Value(buf));

  buf.assign(32, static_cast<char>(0xff));
  EXPECT_EQ(0x62a8ab43u, Value(buf));

  for (int i = 0; i < 32; i++) buf[static_cast<std::size_t>(i)] = static_cast<char>(i);
  EXPECT_EQ(0x46dd794eu, Value(buf));

  for (int i = 0; i < 32; i++) buf[static_cast<std::size_t>(i)] = static_cast<char>(31 - i);
  EXPECT_EQ(0x113fdb5cu, Value(buf));

  const unsigned char iscsi_read[48] = {
      0x01, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
      0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x18, 0x28, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };
  EXPECT_EQ(0xd9963a56u,
            Value(std::string_view(reinterpret_cast<const char*>(iscsi_read), sizeof(iscsi_read))));
}

TEST(Crc32cTest, CheckValue) {
  // The standard "check" value for CRC-32C.
  EXPECT_EQ(0xe3069283u, Value("123456789"));
}

TEST(Crc32cTest, EmptyIsZero) { EXPECT_EQ(0u, Value("")); }

TEST(Crc32cTest, Values) { EXPECT_NE(Value("a"), Value("foo")); }

TEST(Crc32cTest, ExtendComposes) {
  EXPECT_EQ(Value("hello world"), Extend(Value("hello "), "world"));
  const std::string big(100000, 'z');
  EXPECT_EQ(Value(big + "tail"), Extend(Value(big), "tail"));
  EXPECT_EQ(Value("abc"), Extend(Value(""), "abc"));
}

TEST(Crc32cTest, Mask) {
  const std::uint32_t crc = Value("foo");
  EXPECT_NE(crc, Mask(crc));
  EXPECT_NE(crc, Mask(Mask(crc)));
  EXPECT_EQ(crc, Unmask(Mask(crc)));
  EXPECT_EQ(crc, Unmask(Unmask(Mask(Mask(crc)))));
  for (std::uint32_t v : {0u, 1u, 0xffffffffu, 0x80000000u, 0x12345678u}) {
    EXPECT_EQ(v, Unmask(Mask(v))) << v;
  }
}

TEST(Crc32cTest, MaskMatchesSpec) {
  // rotate right by 15, add 0xa282ead8 (mod 2^32).
  const std::uint32_t crc = 0x12345678u;
  const std::uint32_t expected = ((crc >> 15) | (crc << 17)) + 0xa282ead8u;
  EXPECT_EQ(expected, Mask(crc));
}

}  // namespace
}  // namespace strata::crc32c
