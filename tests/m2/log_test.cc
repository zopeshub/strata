// WAL format and recovery semantics. Modelled on LevelDB's log_test.cc,
// which encodes a decade of production lessons about torn writes.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "db/log_reader.h"
#include "db/log_writer.h"
#include "support/mem_fs.h"
#include "support/test_util.h"
#include "util/coding.h"
#include "util/crc32c.h"

namespace strata::log {
namespace {

using testing::MemFileSystem;

// Construct a string of the specified length made out of the supplied
// partial string.
std::string BigString(std::string_view partial, std::size_t n) {
  std::string result;
  while (result.size() < n) result.append(partial);
  result.resize(n);
  return result;
}

std::string NumberString(int n) { return std::to_string(n) + "."; }

std::string RandomSkewedString(int i, Random& rnd) {
  return BigString(NumberString(i), rnd.Skewed(17));
}

class LogTest : public ::testing::Test {
 protected:
  static constexpr const char* kPath = "/wal/000001.log";

  LogTest() {
    EXPECT_OK(fs_.CreateDir("/wal"));
    auto f = fs_.NewWritableFile(kPath);
    EXPECT_TRUE(f.has_value());
    dest_ = std::move(*f);
    writer_ = std::make_unique<Writer>(*dest_);
  }

  void Write(std::string_view msg) { ASSERT_OK(writer_->AddRecord(msg)); }

  std::size_t WrittenBytes() { return fs_.ReadFileToString(kPath)->size(); }

  std::string Read() {
    if (!reader_) StartReading();
    std::string scratch;
    auto r = reader_->ReadRecord(scratch);
    return r ? std::string(*r) : std::string("EOF");
  }

  void StartReading(bool verify_checksums = true) {
    auto f = fs_.NewSequentialFile(kPath);
    ASSERT_TRUE(f.has_value());
    reader_ = std::make_unique<Reader>(
        std::move(*f),
        [this](std::size_t bytes, const Status& reason) {
          dropped_ += bytes;
          ++reports_;
          last_report_ = reason.ToString();
        },
        verify_checksums);
  }

  void IncrementByte(std::size_t offset, int delta) {
    std::string contents = *fs_.ReadFileToString(kPath);
    contents[offset] = static_cast<char>(contents[offset] + delta);
    ASSERT_OK(fs_.WriteStringToFile(kPath, contents, true));
  }

  void SetByte(std::size_t offset, char value) {
    std::string contents = *fs_.ReadFileToString(kPath);
    contents[offset] = value;
    ASSERT_OK(fs_.WriteStringToFile(kPath, contents, true));
  }

  void ShrinkSize(std::size_t bytes) { ASSERT_OK(fs_.Truncate(kPath, WrittenBytes() - bytes)); }

  // Recompute the checksum of the physical record at `header_offset` whose
  // payload is `len` bytes (after the test tampered with type or payload).
  void FixChecksum(std::size_t header_offset, std::size_t len) {
    std::string contents = *fs_.ReadFileToString(kPath);
    const std::string_view type_and_payload(contents.data() + header_offset + 6, 1 + len);
    const std::uint32_t crc = crc32c::Mask(crc32c::Value(type_and_payload));
    EncodeFixed32(contents.data() + header_offset, crc);
    ASSERT_OK(fs_.WriteStringToFile(kPath, contents, true));
  }

  void AppendRaw(std::string_view bytes) {
    auto f = fs_.NewAppendableFile(kPath);
    ASSERT_TRUE(f.has_value());
    ASSERT_OK((*f)->Append(bytes));
  }

  // Reopen the log for append with a fresh writer, as recovery does.
  void ReopenForAppend() {
    writer_.reset();
    dest_.reset();
    auto f = fs_.NewAppendableFile(kPath);
    ASSERT_TRUE(f.has_value());
    dest_ = std::move(*f);
    writer_ = std::make_unique<Writer>(*dest_, dest_->Size());
  }

  MemFileSystem fs_;
  std::unique_ptr<WritableFile> dest_;
  std::unique_ptr<Writer> writer_;
  std::unique_ptr<Reader> reader_;
  std::size_t dropped_ = 0;
  int reports_ = 0;
  std::string last_report_;
};

TEST_F(LogTest, Empty) {
  EXPECT_EQ("EOF", Read());
  EXPECT_EQ(reports_, 0);
}

TEST_F(LogTest, ReadWrite) {
  Write("foo");
  Write("bar");
  Write("");
  Write("xxxx");
  EXPECT_EQ("foo", Read());
  EXPECT_EQ("bar", Read());
  EXPECT_EQ("", Read());
  EXPECT_EQ("xxxx", Read());
  EXPECT_EQ("EOF", Read());
  EXPECT_EQ("EOF", Read()) << "make sure reads at EOF keep working";
  EXPECT_EQ(reports_, 0);
}

TEST_F(LogTest, ExactSizes) {
  Write(std::string(1000, 'a'));
  EXPECT_EQ(WrittenBytes(), 1000 + kHeaderSize);
}

TEST_F(LogTest, ExactSizesWhenFragmented) {
  Write(std::string(kBlockSize, 'a'));
  // FIRST fragment fills block 0 (header + kBlockSize-7 bytes), LAST carries
  // the remaining 7 bytes with its own header.
  EXPECT_EQ(WrittenBytes(), kBlockSize + kHeaderSize + kHeaderSize);
}

TEST_F(LogTest, ManyBlocks) {
  for (int i = 0; i < 100000; i++) Write(NumberString(i));
  for (int i = 0; i < 100000; i++) ASSERT_EQ(NumberString(i), Read());
  EXPECT_EQ("EOF", Read());
}

TEST_F(LogTest, Fragmentation) {
  Write("small");
  Write(BigString("medium", 50000));
  Write(BigString("large", 100000));
  EXPECT_EQ("small", Read());
  EXPECT_EQ(BigString("medium", 50000), Read());
  EXPECT_EQ(BigString("large", 100000), Read());
  EXPECT_EQ("EOF", Read());
}

TEST_F(LogTest, MarginalTrailer) {
  // Make a trailer that is exactly the same length as an empty record.
  const std::size_t n = kBlockSize - 2 * kHeaderSize;
  Write(BigString("foo", n));
  EXPECT_EQ(kBlockSize - kHeaderSize, WrittenBytes());
  Write("");
  Write("bar");
  EXPECT_EQ(BigString("foo", n), Read());
  EXPECT_EQ("", Read());
  EXPECT_EQ("bar", Read());
  EXPECT_EQ("EOF", Read());
}

TEST_F(LogTest, MarginalTrailer2) {
  const std::size_t n = kBlockSize - 2 * kHeaderSize;
  Write(BigString("foo", n));
  EXPECT_EQ(kBlockSize - kHeaderSize, WrittenBytes());
  Write("bar");
  EXPECT_EQ(BigString("foo", n), Read());
  EXPECT_EQ("bar", Read());
  EXPECT_EQ("EOF", Read());
  EXPECT_EQ(0u, dropped_);
  EXPECT_EQ(reports_, 0);
}

TEST_F(LogTest, ShortTrailer) {
  const std::size_t n = kBlockSize - 2 * kHeaderSize + 4;
  Write(BigString("foo", n));
  EXPECT_EQ(kBlockSize - kHeaderSize + 4, WrittenBytes());
  Write("");
  Write("bar");
  EXPECT_EQ(BigString("foo", n), Read());
  EXPECT_EQ("", Read());
  EXPECT_EQ("bar", Read());
  EXPECT_EQ("EOF", Read());
}

TEST_F(LogTest, AlignedEof) {
  const std::size_t n = kBlockSize - 2 * kHeaderSize + 4;
  Write(BigString("foo", n));
  EXPECT_EQ(kBlockSize - kHeaderSize + 4, WrittenBytes());
  EXPECT_EQ(BigString("foo", n), Read());
  EXPECT_EQ("EOF", Read());
}

TEST_F(LogTest, TrailerIsZeroFilled) {
  const std::size_t n = kBlockSize - kHeaderSize - 3;  // leaves 3 bytes in block 0
  Write(BigString("x", n));
  Write("next");
  const std::string contents = *fs_.ReadFileToString(kPath);
  ASSERT_GE(contents.size(), kBlockSize);
  EXPECT_EQ(contents.substr(kBlockSize - 3, 3), std::string(3, '\0'));
}

TEST_F(LogTest, OpenForAppend) {
  Write("hello");
  ReopenForAppend();
  Write("world");
  EXPECT_EQ("hello", Read());
  EXPECT_EQ("world", Read());
  EXPECT_EQ("EOF", Read());
}

TEST_F(LogTest, OpenForAppendNearBlockEnd) {
  // After this, only 3 bytes remain in block 0: the reopened writer must
  // know it has to pad a trailer before its first header.
  Write(BigString("x", kBlockSize - kHeaderSize - 3));
  ReopenForAppend();
  Write("after-reopen");
  EXPECT_EQ(BigString("x", kBlockSize - kHeaderSize - 3), Read());
  EXPECT_EQ("after-reopen", Read());
  EXPECT_EQ("EOF", Read());
  EXPECT_EQ(reports_, 0);
}

TEST_F(LogTest, OpenForAppendMidBlockAcrossManyBlocks) {
  Random rnd(301);
  std::vector<std::string> written;
  for (int round = 0; round < 10; round++) {
    for (int i = 0; i < 50; i++) {
      written.push_back(RandomSkewedString(round * 1000 + i, rnd));
      Write(written.back());
    }
    ReopenForAppend();
  }
  for (const auto& w : written) ASSERT_EQ(w, Read());
  EXPECT_EQ("EOF", Read());
}

TEST_F(LogTest, RandomRead) {
  const int n = 500;
  Random write_rnd(301);
  for (int i = 0; i < n; i++) Write(RandomSkewedString(i, write_rnd));
  Random read_rnd(301);
  for (int i = 0; i < n; i++) ASSERT_EQ(RandomSkewedString(i, read_rnd), Read());
  EXPECT_EQ("EOF", Read());
}

TEST_F(LogTest, BadRecordType) {
  Write("foo");
  IncrementByte(6, 100);  // type byte
  FixChecksum(0, 3);
  EXPECT_EQ("EOF", Read());
  EXPECT_EQ(3u, dropped_);
  EXPECT_EQ(reports_, 1);
}

TEST_F(LogTest, TruncatedTrailingRecordIsIgnored) {
  Write("foo");
  ShrinkSize(4);  // drop all payload and part of the header
  EXPECT_EQ("EOF", Read());
  EXPECT_EQ(0u, dropped_) << "a torn tail is not corruption";
  EXPECT_EQ(reports_, 0);
}

TEST_F(LogTest, TruncatedPayloadIsIgnored) {
  Write("foo");
  ShrinkSize(1);
  EXPECT_EQ("EOF", Read());
  EXPECT_EQ(0u, dropped_);
  EXPECT_EQ(reports_, 0);
}

TEST_F(LogTest, BadLength) {
  const std::size_t payload = kBlockSize - kHeaderSize;
  Write(BigString("bar", payload));
  Write("foo");
  // Least significant length byte is at header[4]; now the record claims to
  // run past the end of block 0.
  IncrementByte(4, 1);
  EXPECT_EQ("foo", Read());
  EXPECT_EQ(kBlockSize, dropped_);
  EXPECT_EQ(reports_, 1);
}

TEST_F(LogTest, ChecksumMismatch) {
  Write("foooooo");
  IncrementByte(0, 14);
  EXPECT_EQ("EOF", Read());
  EXPECT_EQ(14u, dropped_);
  EXPECT_EQ(reports_, 1);
}

TEST_F(LogTest, ChecksumMismatchDropsRestOfBlock) {
  Write("first");                    // FULL at 0..12
  Write("second");                   // FULL at 12..25, payload starts at 19
  Write(BigString("third", 40000));  // FIRST in block 0, LAST in block 1
  Write("fourth");                   // FULL in block 1
  IncrementByte(19, 1);              // corrupt "second"'s payload
  EXPECT_EQ("first", Read());
  EXPECT_EQ("fourth", Read()) << "'second' fails its CRC -> rest of block 0 is dropped, "
                                 "so 'third's LAST fragment has no FIRST and is dropped too";
  EXPECT_EQ("EOF", Read());
  EXPECT_EQ(reports_, 2);
}

TEST_F(LogTest, UnexpectedMiddleType) {
  Write("foo");
  SetByte(6, static_cast<char>(RecordType::kMiddle));
  FixChecksum(0, 3);
  EXPECT_EQ("EOF", Read());
  EXPECT_EQ(3u, dropped_);
  EXPECT_EQ(reports_, 1);
}

TEST_F(LogTest, UnexpectedLastType) {
  Write("foo");
  SetByte(6, static_cast<char>(RecordType::kLast));
  FixChecksum(0, 3);
  EXPECT_EQ("EOF", Read());
  EXPECT_EQ(3u, dropped_);
  EXPECT_EQ(reports_, 1);
}

TEST_F(LogTest, UnexpectedFullType) {
  Write("foo");
  Write("bar");
  SetByte(6, static_cast<char>(RecordType::kFirst));
  FixChecksum(0, 3);
  EXPECT_EQ("bar", Read());
  EXPECT_EQ("EOF", Read());
  EXPECT_EQ(3u, dropped_);
  EXPECT_EQ(reports_, 1);
}

TEST_F(LogTest, UnexpectedFirstType) {
  Write("foo");
  Write(BigString("bar", 100000));
  SetByte(6, static_cast<char>(RecordType::kFirst));
  FixChecksum(0, 3);
  EXPECT_EQ(BigString("bar", 100000), Read());
  EXPECT_EQ("EOF", Read());
  EXPECT_EQ(3u, dropped_);
  EXPECT_EQ(reports_, 1);
}

TEST_F(LogTest, MissingLastIsIgnored) {
  Write(BigString("bar", kBlockSize));
  ShrinkSize(14);  // remove the LAST fragment entirely
  EXPECT_EQ("EOF", Read());
  EXPECT_EQ(reports_, 0);
  EXPECT_EQ(0u, dropped_);
}

TEST_F(LogTest, PartialLastIsIgnored) {
  Write(BigString("bar", kBlockSize));
  ShrinkSize(1);
  EXPECT_EQ("EOF", Read());
  EXPECT_EQ(reports_, 0);
  EXPECT_EQ(0u, dropped_);
}

TEST_F(LogTest, ErrorJoinsRecords) {
  // Consider two fragmented records: first(R1) last(R1) first(R2) last(R2),
  // where the middle two fragments disappear. We must NOT return
  // first(R1)+last(R2) as a single record.
  Write(BigString("foo", kBlockSize));
  Write(BigString("bar", kBlockSize));
  Write("correct");
  for (std::size_t offset = kBlockSize; offset < 2 * kBlockSize; offset++) SetByte(offset, 'x');
  EXPECT_EQ("correct", Read());
  EXPECT_EQ("EOF", Read());
  EXPECT_GE(dropped_, 2 * kBlockSize);
  EXPECT_LE(dropped_, 2 * kBlockSize + 100);
}

TEST_F(LogTest, ZeroedPreallocatedSpaceIsSkippedSilently) {
  Write("foo");
  AppendRaw(std::string(100, '\0'));
  EXPECT_EQ("foo", Read());
  EXPECT_EQ("EOF", Read());
  EXPECT_EQ(reports_, 0);
}

TEST_F(LogTest, NoChecksumVerificationWhenDisabled) {
  Write("foo");
  IncrementByte(0, 1);  // break the CRC only
  StartReading(/*verify_checksums=*/false);
  EXPECT_EQ("foo", Read());
  EXPECT_EQ(reports_, 0);
}

TEST_F(LogTest, LastRecordOffset) {
  Write("a");                             // at 0
  Write("bb");                            // at 8
  Write(BigString("c", kBlockSize * 2));  // at 17
  EXPECT_EQ("a", Read());
  EXPECT_EQ(reader_->LastRecordOffset(), 0u);
  EXPECT_EQ("bb", Read());
  EXPECT_EQ(reader_->LastRecordOffset(), 8u);
  EXPECT_EQ(BigString("c", kBlockSize * 2), Read());
  EXPECT_EQ(reader_->LastRecordOffset(), 17u);
}

}  // namespace
}  // namespace strata::log
