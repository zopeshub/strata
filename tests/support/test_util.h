#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "db/dbformat.h"
#include "strata/db.h"
#include "strata/file_system.h"
#include "strata/iterator.h"
#include "strata/status.h"
#include "util/random.h"

namespace strata::testing {

inline Status GetStatus(const Status& s) { return s; }
template <class T>
Status GetStatus(const Result<T>& r) {
  return r.has_value() ? Status::OK() : r.error();
}

template <class T>
::testing::AssertionResult IsOkFormat(const char* expr_text, const T& value) {
  const Status s = GetStatus(value);
  if (s.ok()) return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure() << expr_text << " -> " << s.ToString();
}

}  // namespace strata::testing

#define STRATA_TEST_CONCAT_INNER(a, b) a##b
#define STRATA_TEST_CONCAT(a, b) STRATA_TEST_CONCAT_INNER(a, b)

// Both accept a Status or a Result<T>, and extra context via <<:
//   ASSERT_OK(db->Flush()) << "after round " << i;
#define ASSERT_OK(expr) ASSERT_PRED_FORMAT1(::strata::testing::IsOkFormat, expr)
#define EXPECT_OK(expr) EXPECT_PRED_FORMAT1(::strata::testing::IsOkFormat, expr)

#define ASSERT_OK_AND_ASSIGN(lhs, expr) \
  ASSERT_OK_AND_ASSIGN_IMPL(STRATA_TEST_CONCAT(strata_r_, __LINE__), lhs, expr)

#define ASSERT_OK_AND_ASSIGN_IMPL(tmp, lhs, expr)                            \
  auto tmp = (expr);                                                         \
  ASSERT_TRUE(tmp.has_value()) << #expr << " -> " << tmp.error().ToString(); \
  lhs = std::move(*tmp)

namespace strata::testing {

// A fresh directory under the system temp dir, removed (recursively) on
// destruction unless STRATA_KEEP_TEST_DIRS is set in the environment.
class TempDir {
 public:
  TempDir();
  ~TempDir();
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  [[nodiscard]] const Path& path() const { return path_; }
  [[nodiscard]] Path operator/(std::string_view name) const { return path_ / name; }

 private:
  Path path_;
};

// "k" followed by `i` zero-padded to `width` digits: sorts numerically.
std::string NumberedKeyImpl(std::uint64_t i, int width);
template <std::integral I>
std::string NumberedKey(I i, int width = 16) {
  return NumberedKeyImpl(static_cast<std::uint64_t>(i), width);
}
// Printable random string.
std::string RandomString(Random& rnd, std::size_t len);
// Arbitrary bytes, including '\0' and 0xff.
std::string RandomBytes(Random& rnd, std::size_t len);
std::string Hex(std::string_view bytes);

// Reads a sequence of (key, value) from a positioned iterator until !Valid(),
// then asserts status().ok() via gtest (non-fatal).
std::vector<std::pair<std::string, std::string>> Drain(Iterator& it);

// Full scan of a DB as a std::map (via NewIterator).
std::map<std::string, std::string> DumpDB(DB& db, const ReadOptions& options = {});

// Sorted, in-memory Iterator over (key, value) pairs.
class VectorIterator final : public Iterator {
 public:
  // REQUIRES: entries sorted by `cmp`. `cmp` must outlive the iterator.
  VectorIterator(std::vector<std::pair<std::string, std::string>> entries, const Comparator& cmp);

  // When the iterator would land on entries[index], it becomes invalid and
  // reports `status` instead.
  void InjectErrorAt(std::size_t index, Status status);

  bool Valid() const override;
  void SeekToFirst() override;
  void Seek(std::string_view target) override;
  void Next() override;
  std::string_view key() const override;
  std::string_view value() const override;
  Status status() const override;

 private:
  void CheckError();

  std::vector<std::pair<std::string, std::string>> entries_;
  const Comparator& cmp_;
  std::size_t pos_;
  std::size_t error_index_ = static_cast<std::size_t>(-1);
  Status error_;
  Status status_;
};

}  // namespace strata::testing
