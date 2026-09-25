#pragma once

// Fixture for DB-level tests, parameterized over the file system:
//   kMem   - MemFileSystem: fast, supports crash simulation
//   kPosix - the real POSIX file system (your M1 code) in a temp dir
//
//   class MyTest : public DbTest {};
//   TEST_P(MyTest, Foo) { auto db = Open(); ... }
//   INSTANTIATE_TEST_SUITE_P(Fs, MyTest, AllFileSystems(), FsName);

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "strata/db.h"
#include "strata/file_system.h"
#include "support/mem_fs.h"
#include "support/test_util.h"

namespace strata::testing {

enum class FsKind { kMem, kPosix };

inline void PrintTo(FsKind k, std::ostream* os) { *os << (k == FsKind::kMem ? "Mem" : "Posix"); }

inline auto AllFileSystems() { return ::testing::Values(FsKind::kMem, FsKind::kPosix); }
inline auto MemOnly() { return ::testing::Values(FsKind::kMem); }

inline std::string FsName(const ::testing::TestParamInfo<FsKind>& info) {
  return info.param == FsKind::kMem ? "Mem" : "Posix";
}

class DbTest : public ::testing::TestWithParam<FsKind> {
 protected:
  DbTest() {
    if (GetParam() == FsKind::kMem) {
      mem_ = std::make_unique<MemFileSystem>();
      dbpath_ = "/db";
      fs_ = mem_.get();
    } else {
      posix_ = NewPosixFileSystem();
      tmp_ = std::make_unique<TempDir>();
      dbpath_ = tmp_->path() / "db";
      fs_ = posix_.get();
    }
    options_.fs = fs_;
  }

  // Opens (creating if needed) with options_. Fails the test on error.
  std::unique_ptr<DB> Open() {
    auto db = DB::Open(options_, dbpath_);
    EXPECT_TRUE(db.has_value()) << db.error().ToString();
    return db.has_value() ? std::move(*db) : nullptr;
  }

  Result<std::unique_ptr<DB>> TryOpen() { return DB::Open(options_, dbpath_); }

  void Reopen(std::unique_ptr<DB>& db) {
    db.reset();
    db = Open();
  }

  // Power-loss crash of an open DB (MemFileSystem only).
  void CrashAndReopen(std::unique_ptr<DB>& db,
                      MemFileSystem::Unsynced mode = MemFileSystem::Unsynced::kDropAll,
                      std::uint64_t seed = 0) {
    ASSERT_NE(mem_, nullptr) << "crash simulation requires MemFileSystem";
    mem_->Freeze();
    db.reset();
    mem_->SimulatePowerLoss(mode, seed);
    mem_->Unfreeze();
    db = Open();
  }

  std::string GetOrDie(DB& db, std::string_view key, const ReadOptions& ro = {}) {
    auto r = db.Get(ro, key);
    EXPECT_TRUE(r.has_value()) << r.error().ToString();
    if (!r.has_value() || !r->has_value()) return "NOT_FOUND";
    return **r;
  }

  int NumFilesAtLevel(DB& db, int level) {
    auto p =
        db.GetProperty(std::string(properties::kNumFilesAtLevelPrefix) + std::to_string(level));
    EXPECT_TRUE(p.has_value()) << "property num-files-at-level" << level << " not supported";
    return p ? std::stoi(*p) : -1;
  }

  int TotalTableFiles(DB& db) {
    int total = 0;
    for (int level = 0; level < 7; ++level) total += NumFilesAtLevel(db, level);
    return total;
  }

  std::uint64_t CounterProperty(DB& db, std::string_view name) {
    auto p = db.GetProperty(name);
    EXPECT_TRUE(p.has_value()) << "property " << name << " not supported";
    return p ? std::stoull(*p) : 0;
  }

  // Count files in the DB dir whose name ends with `suffix`.
  int CountFilesWithSuffix(std::string_view suffix) {
    auto children = fs_->GetChildren(dbpath_);
    EXPECT_TRUE(children.has_value());
    int n = 0;
    if (children) {
      for (const auto& c : *children) {
        if (c.size() >= suffix.size() && c.ends_with(suffix)) ++n;
      }
    }
    return n;
  }

  Options options_;
  Path dbpath_;
  FileSystem* fs_ = nullptr;
  std::unique_ptr<MemFileSystem> mem_;
  std::unique_ptr<FileSystem> posix_;
  std::unique_ptr<TempDir> tmp_;
};

}  // namespace strata::testing
