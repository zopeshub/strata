#include "table/table.h"

#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "db/dbformat.h"
#include "support/mem_fs.h"
#include "support/test_util.h"
#include "table/table_builder.h"

namespace strata {
namespace {

using testing::MemFileSystem;

std::string ValueOf(const LookupResult& r) {
  if (std::holds_alternative<KeyAbsent>(r)) return "ABSENT";
  if (std::holds_alternative<KeyDeleted>(r)) return "DELETED";
  return std::get<std::string>(r);
}

struct Entry {
  std::string user_key;
  SequenceNumber seq;
  ValueType type;
  std::string value;
};

class TableTest : public ::testing::Test {
 protected:
  TableTest() {
    EXPECT_OK(fs_.CreateDir("/t"));
    options_.comparator = &icmp_;
    options_.block_size = 1024;
    options_.block_restart_interval = 16;
    options_.bloom_bits_per_key = 10;
  }

  // Entries must be in internal-key order.
  void BuildTable(const std::vector<Entry>& entries) {
    auto file = fs_.NewWritableFile(kPath);
    ASSERT_TRUE(file.has_value());
    TableBuilder builder(options_, **file);
    for (const auto& e : entries) builder.Add(MakeInternalKey(e.user_key, e.seq, e.type), e.value);
    ASSERT_OK(builder.status());
    ASSERT_OK(builder.Finish());
    EXPECT_EQ(builder.NumEntries(), entries.size());
    ASSERT_OK((*file)->Sync());
    ASSERT_OK((*file)->Close());
    ASSERT_OK_AND_ASSIGN(auto size, fs_.GetFileSize(kPath));
    EXPECT_EQ(builder.FileSize(), size);
  }

  Result<std::unique_ptr<Table>> OpenTable() {
    auto file = fs_.NewRandomAccessFile(kPath);
    if (!file) return Err(file.error());
    const std::uint64_t size = (*file)->Size();
    return Table::Open(options_, std::move(*file), size);
  }

  static std::vector<Entry> Sequential(int n, Random& rnd) {
    std::vector<Entry> out;
    for (int i = 0; i < n; i++) {
      out.push_back({testing::NumberedKey(static_cast<std::uint64_t>(i) * 2, 8),
                     static_cast<SequenceNumber>(i + 1), ValueType::kValue,
                     testing::RandomString(rnd, 100)});
    }
    return out;
  }

  static constexpr const char* kPath = "/t/000001.sst";
  MemFileSystem fs_;
  InternalKeyComparator icmp_;
  TableOptions options_;
};

TEST_F(TableTest, RoundTripIteration) {
  Random rnd(301);
  const auto entries = Sequential(5000, rnd);
  BuildTable(entries);
  ASSERT_OK_AND_ASSIGN(auto table, OpenTable());
  EXPECT_EQ(table->properties().num_entries, 5000u);
  EXPECT_GT(table->properties().num_data_blocks, 100u) << "1 KiB blocks, ~120 B entries";

  auto it = table->NewIterator({});
  it->SeekToFirst();
  std::size_t i = 0;
  for (; it->Valid(); it->Next(), i++) {
    ASSERT_LT(i, entries.size());
    ASSERT_EQ(it->key(), MakeInternalKey(entries[i].user_key, entries[i].seq, entries[i].type));
    ASSERT_EQ(it->value(), entries[i].value);
  }
  EXPECT_OK(it->status());
  EXPECT_EQ(i, entries.size());
}

TEST_F(TableTest, IteratorSeek) {
  Random rnd(1);
  const auto entries = Sequential(3000, rnd);
  BuildTable(entries);
  ASSERT_OK_AND_ASSIGN(auto table, OpenTable());
  auto it = table->NewIterator({});
  for (std::size_t i = 0; i < entries.size(); i += 97) {
    it->Seek(LookupKey(entries[i].user_key, kMaxSequenceNumber).internal_key());
    ASSERT_TRUE(it->Valid());
    EXPECT_EQ(ExtractUserKey(it->key()), entries[i].user_key);
    // Odd numbers are absent: seek lands on the next even key.
    it->Seek(LookupKey(testing::NumberedKey(i * 2 + 1, 8), kMaxSequenceNumber).internal_key());
    if (i + 1 < entries.size()) {
      ASSERT_TRUE(it->Valid());
      EXPECT_EQ(ExtractUserKey(it->key()), entries[i + 1].user_key);
    }
  }
  it->Seek(LookupKey("zzzz", kMaxSequenceNumber).internal_key());
  EXPECT_FALSE(it->Valid());
  EXPECT_OK(it->status());
}

TEST_F(TableTest, PointLookupsAndBloomSavesReads) {
  Random rnd(2);
  const auto entries = Sequential(4000, rnd);
  BuildTable(entries);
  ASSERT_OK_AND_ASSIGN(auto table, OpenTable());
  for (std::size_t i = 0; i < entries.size(); i += 7) {
    ASSERT_OK_AND_ASSIGN(auto r,
                         table->Get({}, LookupKey(entries[i].user_key, kMaxSequenceNumber)));
    ASSERT_EQ(ValueOf(r), entries[i].value);
  }
  const auto before = table->stats();
  for (int i = 0; i < 2000; i++) {
    ASSERT_OK_AND_ASSIGN(
        auto r,
        table->Get({}, LookupKey(testing::NumberedKey(static_cast<std::uint64_t>(i) * 2 + 1, 8),
                                 kMaxSequenceNumber)));
    ASSERT_EQ(ValueOf(r), "ABSENT");
  }
  const auto after = table->stats();
  EXPECT_EQ(after.bloom_checks - before.bloom_checks, 2000u);
  EXPECT_GE(after.bloom_negatives - before.bloom_negatives, 1940u) << "~1% FP rate expected";
  EXPECT_LE(after.data_blocks_read - before.data_blocks_read, 60u)
      << "a bloom-negative lookup must not read a data block";
}

TEST_F(TableTest, VersionsAndTombstones) {
  std::vector<Entry> entries = {
      {"a", 10, ValueType::kValue, "a10"}, {"a", 5, ValueType::kValue, "a5"},
      {"b", 7, ValueType::kDeletion, ""},  {"b", 3, ValueType::kValue, "b3"},
      {"c", 1, ValueType::kValue, "c1"},
  };
  BuildTable(entries);
  ASSERT_OK_AND_ASSIGN(auto table, OpenTable());
  auto get = [&](std::string_view k, SequenceNumber s) {
    auto r = table->Get({}, LookupKey(k, s));
    EXPECT_TRUE(r.has_value());
    return r ? ValueOf(*r) : "ERROR";
  };
  EXPECT_EQ(get("a", 20), "a10");
  EXPECT_EQ(get("a", 10), "a10");
  EXPECT_EQ(get("a", 9), "a5");
  EXPECT_EQ(get("a", 4), "ABSENT");
  EXPECT_EQ(get("b", 100), "DELETED");
  EXPECT_EQ(get("b", 6), "b3");
  EXPECT_EQ(get("b", 2), "ABSENT");
  EXPECT_EQ(get("c", 1), "c1");
  EXPECT_EQ(get("0", 100), "ABSENT");
  EXPECT_EQ(get("d", 100), "ABSENT");
}

TEST_F(TableTest, ManyVersionsSpanningBlocks) {
  std::vector<Entry> entries;
  entries.push_back({"a", 1, ValueType::kValue, "before"});
  for (SequenceNumber s = 300; s >= 1; s--) {
    entries.push_back({"x", s, ValueType::kValue, "x@" + std::to_string(s) + std::string(40, '.')});
  }
  entries.push_back({"z", 1, ValueType::kValue, "after"});
  BuildTable(entries);
  ASSERT_OK_AND_ASSIGN(auto table, OpenTable());
  ASSERT_GT(table->properties().num_data_blocks, 5u);
  for (SequenceNumber s : {1ull, 2ull, 57ull, 150ull, 299ull, 300ull, 1000ull}) {
    ASSERT_OK_AND_ASSIGN(auto r, table->Get({}, LookupKey("x", s)));
    const SequenceNumber expect = std::min<SequenceNumber>(s, 300);
    EXPECT_EQ(ValueOf(r), "x@" + std::to_string(expect) + std::string(40, '.')) << "snapshot " << s;
  }
}

TEST_F(TableTest, WorksWithoutBloom) {
  options_.bloom_bits_per_key = 0;
  Random rnd(3);
  const auto entries = Sequential(500, rnd);
  BuildTable(entries);
  ASSERT_OK_AND_ASSIGN(auto table, OpenTable());
  ASSERT_OK_AND_ASSIGN(auto hit,
                       table->Get({}, LookupKey(entries[250].user_key, kMaxSequenceNumber)));
  EXPECT_EQ(ValueOf(hit), entries[250].value);
  ASSERT_OK_AND_ASSIGN(auto miss, table->Get({}, LookupKey("nope", kMaxSequenceNumber)));
  EXPECT_EQ(ValueOf(miss), "ABSENT");
}

TEST_F(TableTest, EmptyTable) {
  BuildTable({});
  ASSERT_OK_AND_ASSIGN(auto table, OpenTable());
  EXPECT_EQ(table->properties().num_entries, 0u);
  auto it = table->NewIterator({});
  it->SeekToFirst();
  EXPECT_FALSE(it->Valid());
  EXPECT_OK(it->status());
  ASSERT_OK_AND_ASSIGN(auto r, table->Get({}, LookupKey("a", 1)));
  EXPECT_EQ(ValueOf(r), "ABSENT");
}

TEST_F(TableTest, LargeValues) {
  std::vector<Entry> entries;
  Random rnd(4);
  for (int i = 0; i < 20; i++) {
    entries.push_back({testing::NumberedKey(static_cast<std::uint64_t>(i)), 1, ValueType::kValue,
                       testing::RandomBytes(rnd, 100000)});
  }
  BuildTable(entries);
  ASSERT_OK_AND_ASSIGN(auto table, OpenTable());
  auto it = table->NewIterator({});
  it->SeekToFirst();
  for (const auto& e : entries) {
    ASSERT_TRUE(it->Valid());
    ASSERT_EQ(it->value(), e.value);
    it->Next();
  }
  EXPECT_FALSE(it->Valid());
}

TEST_F(TableTest, CorruptDataBlockIsDetected) {
  Random rnd(5);
  const auto entries = Sequential(200, rnd);
  BuildTable(entries);
  ASSERT_OK(fs_.CorruptByte(kPath, 100));
  ASSERT_OK_AND_ASSIGN(auto table, OpenTable());
  ReadOptions verify;
  verify.verify_checksums = true;
  auto it = table->NewIterator(verify);
  int n = 0;
  for (it->SeekToFirst(); it->Valid(); it->Next()) n++;
  EXPECT_TRUE(it->status().IsCorruption())
      << "read " << n << " entries; status " << it->status().ToString();
  auto r = table->Get(verify, LookupKey(entries[0].user_key, kMaxSequenceNumber));
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsCorruption());
}

TEST_F(TableTest, TruncatedFileFailsToOpen) {
  Random rnd(6);
  BuildTable(Sequential(200, rnd));
  ASSERT_OK_AND_ASSIGN(auto size, fs_.GetFileSize(kPath));
  ASSERT_OK(fs_.Truncate(kPath, size - 10));
  auto t = OpenTable();
  ASSERT_FALSE(t.has_value());
  EXPECT_TRUE(t.error().IsCorruption()) << t.error().ToString();
}

TEST_F(TableTest, TinyFileFailsToOpen) {
  ASSERT_OK(fs_.WriteStringToFile(kPath, "not a table", true));
  auto t = OpenTable();
  ASSERT_FALSE(t.has_value());
  EXPECT_TRUE(t.error().IsCorruption());
}

TEST_F(TableTest, AbandonDoesNotWriteATail) {
  auto file = fs_.NewWritableFile(kPath);
  ASSERT_TRUE(file.has_value());
  {
    TableBuilder builder(options_, **file);
    builder.Add(MakeInternalKey("a", 1, ValueType::kValue), "v");
    builder.Abandon();
  }
  ASSERT_OK((*file)->Close());
  auto t = OpenTable();
  EXPECT_FALSE(t.has_value());
}

}  // namespace
}  // namespace strata
