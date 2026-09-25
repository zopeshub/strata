#include "db/filename.h"

#include <gtest/gtest.h>

namespace strata {
namespace {

TEST(FileNameTest, Format) {
  EXPECT_EQ(LogFileName("/db", 7), Path("/db/000007.log"));
  EXPECT_EQ(TableFileName("/db", 12), Path("/db/000012.sst"));
  EXPECT_EQ(ManifestFileName("/db", 5), Path("/db/MANIFEST-000005"));
  EXPECT_EQ(CurrentFileName("/db"), Path("/db/CURRENT"));
  EXPECT_EQ(LockFileName("/db"), Path("/db/LOCK"));
  EXPECT_EQ(TempFileName("/db", 13), Path("/db/000013.dbtmp"));
  EXPECT_EQ(LogFileName("/db", 1234567), Path("/db/1234567.log")) << "wider numbers print in full";
}

TEST(FileNameTest, ParseValid) {
  struct Case {
    std::string_view name;
    std::uint64_t number;
    FileType type;
  };
  const Case cases[] = {
      {"100.log", 100, FileType::kLog},
      {"0.log", 0, FileType::kLog},
      {"000007.log", 7, FileType::kLog},
      {"0.sst", 0, FileType::kTable},
      {"000012.sst", 12, FileType::kTable},
      {"CURRENT", 0, FileType::kCurrent},
      {"LOCK", 0, FileType::kLock},
      {"MANIFEST-2", 2, FileType::kManifest},
      {"MANIFEST-000007", 7, FileType::kManifest},
      {"000013.dbtmp", 13, FileType::kTemp},
      {"18446744073709551615.log", 18446744073709551615ull, FileType::kLog},
  };
  for (const auto& c : cases) {
    auto parsed = ParseFileName(c.name);
    ASSERT_TRUE(parsed.has_value()) << c.name;
    EXPECT_EQ(parsed->number, c.number) << c.name;
    EXPECT_EQ(parsed->type, c.type) << c.name;
  }
}

TEST(FileNameTest, ParseRejectsGarbage) {
  const std::string_view bad[] = {
      "",
      "foo",
      "foo-dx-100.log",
      ".log",
      "manifest",
      "CURREN",
      "CURRENTX",
      "MANIFES",
      "MANIFEST",
      "MANIFEST-",
      "XMANIFEST-3",
      "MANIFEST-3x",
      "LOC",
      "LOCKx",
      "LO",
      "100",
      "100.",
      "100.lop",
      "100.LOG",
      "100.log.bak",
      "abc.log",
      "-1.log",
      "+1.log",
      " 1.log",
      "1 .log",
      "18446744073709551616.log",  // overflows uint64
      "184467440737095516150.log",
  };
  for (auto name : bad) {
    EXPECT_FALSE(ParseFileName(name).has_value()) << "should reject: '" << name << "'";
  }
}

TEST(FileNameTest, FormatParseRoundTrip) {
  for (std::uint64_t n : {0ull, 1ull, 999999ull, 1000000ull, 1ull << 40}) {
    EXPECT_EQ(ParseFileName(LogFileName("/d", n).filename().string()),
              (ParsedFileName{n, FileType::kLog}));
    EXPECT_EQ(ParseFileName(TableFileName("/d", n).filename().string()),
              (ParsedFileName{n, FileType::kTable}));
    EXPECT_EQ(ParseFileName(ManifestFileName("/d", n).filename().string()),
              (ParsedFileName{n, FileType::kManifest}));
    EXPECT_EQ(ParseFileName(TempFileName("/d", n).filename().string()),
              (ParsedFileName{n, FileType::kTemp}));
  }
}

}  // namespace
}  // namespace strata
