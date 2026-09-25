#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "strata/file_system.h"
#include "support/test_util.h"

namespace strata {
namespace {

using testing::TempDir;

class PosixFsTest : public ::testing::TestWithParam<bool /*use_mmap_reads*/> {
 protected:
  PosixFsTest()
      : fs_(NewPosixFileSystem({.use_mmap_reads = GetParam(), .writable_buffer_size = 4096})) {}

  void WriteFile(const Path& p, std::string_view data) {
    ASSERT_OK_AND_ASSIGN(auto f, fs_->NewWritableFile(p));
    ASSERT_OK(f->Append(data));
    ASSERT_OK(f->Sync());
    ASSERT_OK(f->Close());
  }

  std::string ReadAll(const Path& p, std::size_t chunk = 7919) {
    auto f = fs_->NewSequentialFile(p);
    EXPECT_TRUE(f.has_value()) << f.error().ToString();
    if (!f) return {};
    std::string out;
    std::string scratch(chunk, '\0');
    while (true) {
      auto r = (*f)->Read(scratch);
      EXPECT_TRUE(r.has_value());
      if (!r || r->empty()) break;
      out.append(*r);
    }
    return out;
  }

  TempDir dir_;
  std::unique_ptr<FileSystem> fs_;
};

TEST_P(PosixFsTest, WriteThenReadSequential) {
  Random rnd(301);
  const std::string data = testing::RandomBytes(rnd, 1 << 20);
  ASSERT_OK_AND_ASSIGN(auto f, fs_->NewWritableFile(dir_ / "f"));
  std::size_t pos = 0;
  while (pos < data.size()) {
    // Mix of tiny appends (buffered), and ones larger than the buffer (bypass).
    const std::size_t n = std::min<std::size_t>(data.size() - pos, 1 + rnd.Skewed(15));
    ASSERT_OK(f->Append(std::string_view(data).substr(pos, n)));
    pos += n;
    ASSERT_EQ(f->Size(), pos);
  }
  ASSERT_OK(f->Sync());
  ASSERT_OK(f->Close());
  EXPECT_EQ(ReadAll(dir_ / "f"), data);
  ASSERT_OK_AND_ASSIGN(auto size, fs_->GetFileSize(dir_ / "f"));
  EXPECT_EQ(size, data.size());
}

TEST_P(PosixFsTest, FlushMakesDataVisibleToOtherReaders) {
  ASSERT_OK_AND_ASSIGN(auto f, fs_->NewWritableFile(dir_ / "f"));
  ASSERT_OK(f->Append("abc"));
  ASSERT_OK(f->Flush());
  EXPECT_EQ(ReadAll(dir_ / "f"), "abc") << "Flush() must hand the bytes to the kernel";
  ASSERT_OK(f->Close());
}

TEST_P(PosixFsTest, DestructorWithoutCloseStillWritesBufferedData) {
  {
    ASSERT_OK_AND_ASSIGN(auto f, fs_->NewWritableFile(dir_ / "f"));
    ASSERT_OK(f->Append("buffered"));
  }
  EXPECT_EQ(ReadAll(dir_ / "f"), "buffered");
}

TEST_P(PosixFsTest, NewWritableFileTruncates) {
  WriteFile(dir_ / "f", "old contents");
  WriteFile(dir_ / "f", "new");
  EXPECT_EQ(ReadAll(dir_ / "f"), "new");
}

TEST_P(PosixFsTest, AppendableFileAppends) {
  WriteFile(dir_ / "f", "hello ");
  ASSERT_OK_AND_ASSIGN(auto f, fs_->NewAppendableFile(dir_ / "f"));
  EXPECT_EQ(f->Size(), 6u) << "Size() starts at the existing length";
  ASSERT_OK(f->Append("world"));
  EXPECT_EQ(f->Size(), 11u);
  ASSERT_OK(f->Close());
  EXPECT_EQ(ReadAll(dir_ / "f"), "hello world");
}

TEST_P(PosixFsTest, AppendableFileCreatesMissing) {
  ASSERT_OK_AND_ASSIGN(auto f, fs_->NewAppendableFile(dir_ / "new"));
  EXPECT_EQ(f->Size(), 0u);
  ASSERT_OK(f->Append("x"));
  ASSERT_OK(f->Close());
  EXPECT_EQ(ReadAll(dir_ / "new"), "x");
}

TEST_P(PosixFsTest, SequentialSkip) {
  WriteFile(dir_ / "f", "0123456789");
  ASSERT_OK_AND_ASSIGN(auto f, fs_->NewSequentialFile(dir_ / "f"));
  ASSERT_OK(f->Skip(4));
  char buf[3];
  ASSERT_OK_AND_ASSIGN(auto v, f->Read(buf));
  EXPECT_EQ(v, "456");
}

TEST_P(PosixFsTest, RandomAccessReads) {
  WriteFile(dir_ / "f", "0123456789");
  ASSERT_OK_AND_ASSIGN(auto f, fs_->NewRandomAccessFile(dir_ / "f"));
  EXPECT_EQ(f->Size(), 10u);
  char buf[4];
  ASSERT_OK_AND_ASSIGN(auto a, f->Read(0, buf));
  EXPECT_EQ(a, "0123");
  ASSERT_OK_AND_ASSIGN(auto b, f->Read(3, buf));
  EXPECT_EQ(b, "3456");
  ASSERT_OK_AND_ASSIGN(auto c, f->Read(8, buf));
  EXPECT_EQ(c, "89") << "short read only at EOF";
  ASSERT_OK_AND_ASSIGN(auto d, f->Read(10, buf));
  EXPECT_TRUE(d.empty());
  ASSERT_OK_AND_ASSIGN(auto e, f->Read(1000, buf));
  EXPECT_TRUE(e.empty());
}

TEST_P(PosixFsTest, RandomAccessEmptyFile) {
  WriteFile(dir_ / "empty", "");
  ASSERT_OK_AND_ASSIGN(auto f, fs_->NewRandomAccessFile(dir_ / "empty"));
  EXPECT_EQ(f->Size(), 0u);
  char buf[4];
  ASSERT_OK_AND_ASSIGN(auto v, f->Read(0, buf));
  EXPECT_TRUE(v.empty());
}

TEST_P(PosixFsTest, RandomAccessIsThreadSafe) {
  Random rnd(7);
  const std::string data = testing::RandomBytes(rnd, 256 << 10);
  WriteFile(dir_ / "f", data);
  ASSERT_OK_AND_ASSIGN(auto f, fs_->NewRandomAccessFile(dir_ / "f"));
  std::atomic<int> failures{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; t++) {
    threads.emplace_back([&, t] {
      Random r(static_cast<std::uint64_t>(t) + 100);
      std::string scratch(512, '\0');
      for (int i = 0; i < 2000; i++) {
        const std::uint64_t off = r.Uniform(data.size() - scratch.size());
        auto got = f->Read(off, scratch);
        if (!got || *got != std::string_view(data).substr(off, scratch.size())) ++failures;
      }
    });
  }
  for (auto& t : threads) t.join();
  EXPECT_EQ(failures.load(), 0) << "reads must not share a file offset (use pread)";
}

TEST_P(PosixFsTest, MissingFilesAreNotFound) {
  auto s = fs_->NewSequentialFile(dir_ / "nope");
  ASSERT_FALSE(s.has_value());
  EXPECT_TRUE(s.error().IsNotFound()) << s.error().ToString();
  auto r = fs_->NewRandomAccessFile(dir_ / "nope");
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsNotFound()) << r.error().ToString();
  EXPECT_FALSE(fs_->GetFileSize(dir_ / "nope").has_value());
  EXPECT_FALSE(fs_->RemoveFile(dir_ / "nope").ok());
  EXPECT_FALSE(fs_->FileExists(dir_ / "nope"));
}

TEST_P(PosixFsTest, GetChildren) {
  WriteFile(dir_ / "a.log", "");
  WriteFile(dir_ / "b.sst", "");
  ASSERT_OK(fs_->CreateDir(dir_ / "sub"));
  ASSERT_OK_AND_ASSIGN(auto names, fs_->GetChildren(dir_.path()));
  std::ranges::sort(names);
  EXPECT_EQ(names, (std::vector<std::string>{"a.log", "b.sst", "sub"}));
}

TEST_P(PosixFsTest, RemoveFile) {
  WriteFile(dir_ / "f", "x");
  EXPECT_TRUE(fs_->FileExists(dir_ / "f"));
  ASSERT_OK(fs_->RemoveFile(dir_ / "f"));
  EXPECT_FALSE(fs_->FileExists(dir_ / "f"));
}

TEST_P(PosixFsTest, RenameReplacesAtomically) {
  WriteFile(dir_ / "a", "A");
  WriteFile(dir_ / "b", "B");
  ASSERT_OK(fs_->RenameFile(dir_ / "a", dir_ / "b"));
  EXPECT_FALSE(fs_->FileExists(dir_ / "a"));
  EXPECT_EQ(ReadAll(dir_ / "b"), "A");
}

TEST_P(PosixFsTest, CreateDirIsIdempotentAndRemoveDirWorks) {
  ASSERT_OK(fs_->CreateDir(dir_ / "d"));
  ASSERT_OK(fs_->CreateDir(dir_ / "d"));
  EXPECT_TRUE(fs_->FileExists(dir_ / "d"));
  ASSERT_OK(fs_->SyncDir(dir_ / "d"));
  ASSERT_OK(fs_->RemoveDir(dir_ / "d"));
  EXPECT_FALSE(fs_->FileExists(dir_ / "d"));
}

TEST_P(PosixFsTest, SyncDirOnMissingDirFails) { EXPECT_FALSE(fs_->SyncDir(dir_ / "missing").ok()); }

TEST_P(PosixFsTest, LockIsExclusiveEvenWithinOneProcess) {
  ASSERT_OK_AND_ASSIGN(auto lock, fs_->LockFile(dir_ / "LOCK"));
  auto second = fs_->LockFile(dir_ / "LOCK");
  ASSERT_FALSE(second.has_value()) << "second lock in the same process must fail";
  EXPECT_TRUE(second.error().IsBusy()) << second.error().ToString();
  lock.reset();
  ASSERT_OK_AND_ASSIGN(auto again, fs_->LockFile(dir_ / "LOCK"));
}

TEST_P(PosixFsTest, LockFromSecondFileSystemInstanceFails) {
  auto other = NewPosixFileSystem();
  ASSERT_OK_AND_ASSIGN(auto lock, fs_->LockFile(dir_ / "LOCK"));
  auto second = other->LockFile(dir_ / "LOCK");
  ASSERT_FALSE(second.has_value());
  EXPECT_TRUE(second.error().IsBusy()) << second.error().ToString();
}

TEST_P(PosixFsTest, LockSurvivesClosingAnotherFdOfTheSameFile) {
  // The classic fcntl(F_SETLK) trap: closing ANY fd of the file drops the
  // process's lock. With flock(2) this test passes.
  ASSERT_OK_AND_ASSIGN(auto lock, fs_->LockFile(dir_ / "LOCK"));
  {
    ASSERT_OK_AND_ASSIGN(auto reader, fs_->NewSequentialFile(dir_ / "LOCK"));
  }  // closes an unrelated fd to LOCK
  auto second = NewPosixFileSystem()->LockFile(dir_ / "LOCK");
  EXPECT_FALSE(second.has_value()) << "lock was silently dropped";
}

TEST(PosixFsDefaultTest, DefaultFileSystemIsASingleton) {
  FileSystem* a = DefaultFileSystem();
  FileSystem* b = DefaultFileSystem();
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a, b);
}

INSTANTIATE_TEST_SUITE_P(ReadMode, PosixFsTest, ::testing::Bool(),
                         [](const ::testing::TestParamInfo<bool>& param_info) {
                           return param_info.param ? "Mmap" : "Pread";
                         });

}  // namespace
}  // namespace strata
