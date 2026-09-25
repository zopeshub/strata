#include "strata/write_batch.h"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "support/test_util.h"
#include "util/coding.h"

namespace strata {
namespace {

// Renders a batch as "Put(k,v)Delete(k)..." to compare against expectations.
std::string Render(const WriteBatch& b, Status* status = nullptr) {
  std::string out;
  Status s = b.ForEach([&](const BatchOp& op) {
    std::visit(Overloaded{
                   [&](const PutOp& p) {
                     out += "Put(" + std::string(p.key) + "," + std::string(p.value) + ")";
                   },
                   [&](const DeleteOp& d) { out += "Delete(" + std::string(d.key) + ")"; },
               },
               op);
  });
  if (status != nullptr) {
    *status = s;
  } else {
    EXPECT_TRUE(s.ok()) << s.ToString();
  }
  return out;
}

TEST(WriteBatchTest, Empty) {
  WriteBatch b;
  EXPECT_EQ(b.Count(), 0u);
  EXPECT_EQ(b.Sequence(), 0u);
  EXPECT_EQ(b.Contents().size(), 12u) << "header is fixed64 seq + fixed32 count";
  EXPECT_EQ(Render(b), "");
}

TEST(WriteBatchTest, OpsInOrder) {
  WriteBatch b;
  b.Put("foo", "bar");
  b.Delete("box");
  b.Put("baz", "boo");
  EXPECT_EQ(b.Count(), 3u);
  EXPECT_EQ(Render(b), "Put(foo,bar)Delete(box)Put(baz,boo)");
}

TEST(WriteBatchTest, ExactWireFormat) {
  WriteBatch b;
  b.SetSequence(100);
  b.Put("foo", "bar");
  b.Delete("x");
  std::string expected;
  PutFixed64(expected, 100);
  PutFixed32(expected, 2);
  expected.push_back('\x01');
  PutLengthPrefixed(expected, "foo");
  PutLengthPrefixed(expected, "bar");
  expected.push_back('\x00');
  PutLengthPrefixed(expected, "x");
  EXPECT_EQ(b.Contents(), expected);
}

TEST(WriteBatchTest, Sequence) {
  WriteBatch b;
  b.SetSequence(0x0102030405060708ull);
  EXPECT_EQ(b.Sequence(), 0x0102030405060708ull);
  b.Put("a", "b");
  EXPECT_EQ(b.Sequence(), 0x0102030405060708ull) << "Put must not disturb the header";
}

TEST(WriteBatchTest, EmptyKeysAndValuesAndBinary) {
  WriteBatch b;
  const std::string binary("\0\xff\0", 3);
  b.Put("", "");
  b.Put(binary, binary);
  b.Delete("");
  std::vector<std::pair<std::string, std::string>> puts;
  int deletes = 0;
  ASSERT_OK(b.ForEach([&](const BatchOp& op) {
    if (auto* p = std::get_if<PutOp>(&op)) puts.emplace_back(p->key, p->value);
    else ++deletes;
  }));
  ASSERT_EQ(puts.size(), 2u);
  EXPECT_EQ(puts[0], std::make_pair(std::string(), std::string()));
  EXPECT_EQ(puts[1], std::make_pair(binary, binary));
  EXPECT_EQ(deletes, 1);
}

TEST(WriteBatchTest, LargeValue) {
  WriteBatch b;
  const std::string big(1 << 20, 'v');
  b.Put("k", big);
  std::size_t seen = 0;
  ASSERT_OK(b.ForEach([&](const BatchOp& op) { seen = std::get<PutOp>(op).value.size(); }));
  EXPECT_EQ(seen, big.size());
  EXPECT_GE(b.ApproximateSize(), big.size());
}

TEST(WriteBatchTest, Append) {
  WriteBatch a, b;
  a.SetSequence(200);
  b.SetSequence(300);
  a.Append(b);
  EXPECT_EQ(Render(a), "");
  EXPECT_EQ(a.Count(), 0u);
  b.Put("a", "va");
  a.Append(b);
  EXPECT_EQ(Render(a), "Put(a,va)");
  b.Clear();
  b.Put("b", "vb");
  b.Delete("foo");
  a.Append(b);
  EXPECT_EQ(Render(a), "Put(a,va)Put(b,vb)Delete(foo)");
  EXPECT_EQ(a.Count(), 3u);
  EXPECT_EQ(a.Sequence(), 200u) << "Append keeps the destination's sequence";
}

TEST(WriteBatchTest, Clear) {
  WriteBatch b;
  b.SetSequence(9);
  b.Put("a", "b");
  b.Clear();
  EXPECT_EQ(b.Count(), 0u);
  EXPECT_EQ(b.Contents().size(), 12u);
  EXPECT_EQ(Render(b), "");
}

TEST(WriteBatchTest, IsAValueType) {
  WriteBatch a;
  a.Put("k", "v1");
  WriteBatch copy = a;
  a.Put("k2", "v2");
  EXPECT_EQ(Render(copy), "Put(k,v1)");
  EXPECT_EQ(Render(a), "Put(k,v1)Put(k2,v2)");
  WriteBatch moved = std::move(a);
  EXPECT_EQ(Render(moved), "Put(k,v1)Put(k2,v2)");
}

TEST(WriteBatchTest, FromContentsRoundTrip) {
  WriteBatch b;
  b.SetSequence(77);
  b.Put("x", "y");
  b.Delete("z");
  ASSERT_OK_AND_ASSIGN(WriteBatch copy, WriteBatch::FromContents(b.Contents()));
  EXPECT_EQ(copy.Sequence(), 77u);
  EXPECT_EQ(copy.Count(), 2u);
  EXPECT_EQ(Render(copy), "Put(x,y)Delete(z)");
}

TEST(WriteBatchTest, FromContentsRejectsShortHeader) {
  auto r = WriteBatch::FromContents("too short");
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsCorruption());
}

TEST(WriteBatchTest, DetectsTruncatedOp) {
  WriteBatch b;
  b.Put("foo", "bar");
  std::string rep(b.Contents());
  rep.pop_back();
  ASSERT_OK_AND_ASSIGN(WriteBatch bad, WriteBatch::FromContents(rep));
  Status s;
  Render(bad, &s);
  EXPECT_TRUE(s.IsCorruption()) << s.ToString();
}

TEST(WriteBatchTest, DetectsUnknownTag) {
  WriteBatch b;
  b.Put("foo", "bar");
  std::string rep(b.Contents());
  rep[12] = '\x07';
  ASSERT_OK_AND_ASSIGN(WriteBatch bad, WriteBatch::FromContents(rep));
  Status s;
  Render(bad, &s);
  EXPECT_TRUE(s.IsCorruption()) << s.ToString();
}

TEST(WriteBatchTest, DetectsCountMismatch) {
  WriteBatch b;
  b.Put("foo", "bar");
  std::string rep(b.Contents());
  EncodeFixed32(rep.data() + 8, 2);  // header claims 2 ops, rep has 1
  ASSERT_OK_AND_ASSIGN(WriteBatch bad, WriteBatch::FromContents(rep));
  Status s;
  Render(bad, &s);
  EXPECT_TRUE(s.IsCorruption()) << s.ToString();
}

TEST(WriteBatchTest, ForEachDoesNotCopyTheCallable) {
  struct Counter {
    int* calls;
    void operator()(const BatchOp&) { ++*calls; }
  };
  int calls = 0;
  Counter counter{&calls};
  WriteBatch b;
  b.Put("a", "1");
  b.Put("b", "2");
  ASSERT_OK(b.ForEach(counter));
  EXPECT_EQ(calls, 2);
}

TEST(WriteBatchTest, ForEachAcceptsMoveOnlyCallables) {
  // If this fails to COMPILE, your type erasure requires a copyable callable
  // (e.g. std::function). Rethink it.
  auto owned = std::make_unique<int>(0);
  WriteBatch b;
  b.Put("a", "1");
  ASSERT_OK(b.ForEach([p = std::move(owned)](const BatchOp&) mutable { ++*p; }));
}

}  // namespace
}  // namespace strata
