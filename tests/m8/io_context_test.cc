#include "io/io_context.h"

#include <fcntl.h>
#include <unistd.h>

#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "support/test_util.h"

namespace strata {
namespace {

using testing::TempDir;

enum class Backend { kBlocking, kIoUring };

std::string BackendName(const ::testing::TestParamInfo<Backend>& info) {
  return info.param == Backend::kBlocking ? "Blocking" : "IoUring";
}

auto Backends() {
#if defined(STRATA_HAVE_IO_URING)
  return ::testing::Values(Backend::kBlocking, Backend::kIoUring);
#else
  return ::testing::Values(Backend::kBlocking);
#endif
}

class IoContextTest : public ::testing::TestWithParam<Backend> {
 protected:
  void SetUp() override {
    if (GetParam() == Backend::kBlocking) {
      io_ = NewBlockingIoContext();
    } else {
#if defined(STRATA_HAVE_IO_URING)
      auto r = NewIoUringContext(64);
      if (!r) GTEST_SKIP() << "io_uring unavailable here: " << r.error().ToString();
      io_ = std::move(*r);
#endif
    }
    Random rnd(1);
    contents_ = testing::RandomBytes(rnd, kFileSize);
    const Path path = dir_ / "data";
    const int wfd = ::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    ASSERT_GE(wfd, 0);
    ASSERT_EQ(::write(wfd, contents_.data(), contents_.size()),
              static_cast<ssize_t>(contents_.size()));
    ::close(wfd);
    fd_ = ::open(path.c_str(), O_RDONLY);
    ASSERT_GE(fd_, 0);
  }
  void TearDown() override {
    if (fd_ >= 0) ::close(fd_);
  }

  static constexpr std::size_t kFileSize = 1 << 20;
  TempDir dir_;
  std::unique_ptr<IoContext> io_;
  std::string contents_;
  int fd_ = -1;
};

Task<Result<std::string>> ReadRange(IoContext& io, int fd, std::uint64_t offset, std::size_t len) {
  std::string out(len, '\0');
  std::size_t done = 0;
  while (done < len) {
    auto n = co_await io.Read(fd, offset + done, std::span<char>(out.data() + done, len - done));
    if (!n) co_return Err(n.error());
    if (*n == 0) break;  // EOF
    done += *n;
  }
  out.resize(done);
  co_return out;
}

TEST_P(IoContextTest, ReadsWholeFile) {
  auto r = io_->RunUntilComplete(ReadRange(*io_, fd_, 0, kFileSize));
  ASSERT_OK(r);
  EXPECT_TRUE(*r == contents_);
}

TEST_P(IoContextTest, ShortReadAtEof) {
  auto r = io_->RunUntilComplete(ReadRange(*io_, fd_, kFileSize - 10, 100));
  ASSERT_OK(r);
  EXPECT_EQ(*r, contents_.substr(kFileSize - 10));
  auto past = io_->RunUntilComplete(ReadRange(*io_, fd_, kFileSize + 4096, 100));
  ASSERT_OK(past);
  EXPECT_TRUE(past->empty());
}

TEST_P(IoContextTest, BadFdIsAnError) {
  auto r = io_->RunUntilComplete(ReadRange(*io_, 12345, 0, 10));
  ASSERT_FALSE(r.has_value());
  EXPECT_TRUE(r.error().IsIOError()) << r.error().ToString();
}

TEST_P(IoContextTest, ManySequentialReadsDoNotGrowTheStack) {
  // 1-byte reads, one after another, from one coroutine. If completion
  // resumes the coroutine inline from SubmitRead, each iteration nests
  // deeper and this overflows the stack.
  auto body = [](IoContext& io, int fd) -> Task<Result<std::uint64_t>> {
    std::uint64_t sum = 0;
    char c = 0;
    for (std::uint64_t i = 0; i < 200'000; i++) {
      auto n = co_await io.Read(fd, i % 4096, std::span<char>(&c, 1));
      if (!n) co_return Err(n.error());
      sum += static_cast<unsigned char>(c);
    }
    co_return sum;
  };
  std::uint64_t expected = 0;
  for (std::uint64_t i = 0; i < 200'000; i++)
    expected += static_cast<unsigned char>(contents_[i % 4096]);
  auto r = io_->RunUntilComplete(body(*io_, fd_));
  ASSERT_OK(r);
  EXPECT_EQ(*r, expected);
}

TEST_P(IoContextTest, ManyConcurrentReadsWithWhenAll) {
  constexpr std::size_t kChunk = 4096;
  constexpr std::size_t kReads = 256;
  auto body = [](IoContext& io, int fd) -> Task<std::vector<Result<std::string>>> {
    std::vector<Task<Result<std::string>>> reads;
    for (std::size_t i = 0; i < kReads; i++) {
      // Scattered offsets, so a working backend has many in flight at once.
      const std::uint64_t off = ((i * 7919) % (kFileSize / kChunk)) * kChunk;
      reads.push_back(ReadRange(io, fd, off, kChunk));
    }
    co_return co_await WhenAll(std::move(reads));
  };
  auto results = io_->RunUntilComplete(body(*io_, fd_));
  ASSERT_EQ(results.size(), kReads);
  for (std::size_t i = 0; i < kReads; i++) {
    ASSERT_OK(results[i]);
    const std::uint64_t off = ((i * 7919) % (kFileSize / kChunk)) * kChunk;
    ASSERT_TRUE(*results[i] == contents_.substr(off, kChunk))
        << "read " << i << " returned wrong bytes";
  }
}

TEST_P(IoContextTest, ContextIsReusable) {
  for (int round = 0; round < 3; round++) {
    auto r = io_->RunUntilComplete(ReadRange(*io_, fd_, 100, 100));
    ASSERT_OK(r);
    EXPECT_EQ(*r, contents_.substr(100, 100));
  }
}

INSTANTIATE_TEST_SUITE_P(Backends, IoContextTest, Backends(), BackendName);

}  // namespace
}  // namespace strata
