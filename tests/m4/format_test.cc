#include "table/format.h"

#include <gtest/gtest.h>

#include "support/mem_fs.h"
#include "support/test_util.h"
#include "util/coding.h"
#include "util/crc32c.h"

namespace strata {
namespace {

TEST(BlockHandleTest, RoundTrip) {
  for (std::uint64_t off : {0ull, 1ull, 4096ull, 1ull << 40}) {
    for (std::uint64_t size : {0ull, 17ull, 1ull << 20}) {
      BlockHandle h{off, size};
      std::string enc;
      h.EncodeTo(enc);
      EXPECT_LE(enc.size(), BlockHandle::kMaxEncodedLength);
      std::string_view in = enc;
      auto dec = BlockHandle::DecodeFrom(in);
      ASSERT_TRUE(dec.has_value());
      EXPECT_EQ(*dec, h);
      EXPECT_TRUE(in.empty());
    }
  }
}

TEST(BlockHandleTest, RejectsTruncated) {
  BlockHandle h{1ull << 40, 1ull << 30};
  std::string enc;
  h.EncodeTo(enc);
  for (std::size_t len = 0; len < enc.size(); len++) {
    std::string_view in(enc.data(), len);
    EXPECT_FALSE(BlockHandle::DecodeFrom(in).has_value()) << len;
  }
}

TEST(FooterTest, RoundTripAndFixedSize) {
  Footer f;
  f.metaindex_handle = {12345, 67};
  f.index_handle = {99999, 4321};
  std::string enc;
  f.EncodeTo(enc);
  ASSERT_EQ(enc.size(), Footer::kEncodedLength);
  EXPECT_EQ(DecodeFixed64(enc.data() + enc.size() - 8), kTableMagicNumber);
  EXPECT_EQ(enc.substr(enc.size() - 8), "strata01");
  ASSERT_OK_AND_ASSIGN(Footer dec, Footer::DecodeFrom(enc));
  EXPECT_EQ(dec.metaindex_handle, f.metaindex_handle);
  EXPECT_EQ(dec.index_handle, f.index_handle);
}

TEST(FooterTest, BadMagicIsCorruption) {
  Footer f;
  std::string enc;
  f.EncodeTo(enc);
  enc.back() ^= 0x1;
  auto r = Footer::DecodeFrom(enc);
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsCorruption());
}

TEST(FooterTest, WrongSizeIsCorruption) {
  auto r = Footer::DecodeFrom("short");
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsCorruption());
}

class ReadBlockTest : public ::testing::Test {
 protected:
  // Writes "junk" + block + trailer and returns the handle.
  BlockHandle WriteBlock(std::string_view contents) {
    std::string file = "junk";
    BlockHandle h{file.size(), contents.size()};
    file.append(contents);
    file.push_back(static_cast<char>(CompressionType::kNone));
    const std::uint32_t crc = crc32c::Extend(crc32c::Value(contents), std::string_view("\0", 1));
    PutFixed32(file, crc32c::Mask(crc));
    EXPECT_OK(fs_.CreateDir("/t"));
    EXPECT_OK(fs_.WriteStringToFile("/t/f", file, true));
    return h;
  }

  std::unique_ptr<RandomAccessFile> Open() {
    auto f = fs_.NewRandomAccessFile("/t/f");
    EXPECT_TRUE(f.has_value());
    return std::move(*f);
  }

  testing::MemFileSystem fs_;
};

TEST_F(ReadBlockTest, ReadsAndVerifies) {
  const BlockHandle h = WriteBlock("hello block");
  auto file = Open();
  ASSERT_OK_AND_ASSIGN(auto contents, ReadBlock(*file, h, /*verify_checksums=*/true));
  EXPECT_EQ(contents, "hello block");
}

TEST_F(ReadBlockTest, DetectsCorruption) {
  const BlockHandle h = WriteBlock("hello block");
  ASSERT_OK(fs_.CorruptByte("/t/f", 4 + 3));
  auto file = Open();
  auto r = ReadBlock(*file, h, true);
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsCorruption());
  ASSERT_OK_AND_ASSIGN(auto unverified, ReadBlock(*file, h, false));
  EXPECT_EQ(unverified.size(), h.size);
}

TEST_F(ReadBlockTest, TruncatedFileIsCorruption) {
  const BlockHandle h = WriteBlock("hello block");
  ASSERT_OK(fs_.Truncate("/t/f", 10));
  auto file = Open();
  auto r = ReadBlock(*file, h, true);
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsCorruption()) << r.error().ToString();
}

TEST_F(ReadBlockTest, UnknownCompressionIsCorruption) {
  const BlockHandle h = WriteBlock("abc");
  // Change the type byte and fix the CRC so only the type is wrong.
  std::string file = *fs_.ReadFileToString("/t/f");
  file[4 + 3] = '\x09';
  const std::uint32_t crc = crc32c::Extend(crc32c::Value("abc"), std::string_view("\x09", 1));
  EncodeFixed32(file.data() + 4 + 3 + 1, crc32c::Mask(crc));
  ASSERT_OK(fs_.WriteStringToFile("/t/f", file, true));
  auto f = Open();
  auto r = ReadBlock(*f, h, true);
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsCorruption() || r.error().code() == StatusCode::kNotSupported);
}

}  // namespace
}  // namespace strata
