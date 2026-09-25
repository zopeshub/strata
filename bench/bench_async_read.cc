// M8 + curriculum/06-io-uring.md. Random 4 KiB reads from one thread:
// blocking pread (queue depth 1, always) vs io_uring at queue depth
// 1..64, issued as WhenAll batches.
//
//   STRATA_BENCH_DIR=~/strata-data STRATA_BENCH_DIRECT=1 ./bench_async_read
//
// STRATA_BENCH_DIRECT=1 bypasses the page cache (O_DIRECT on Linux,
// F_NOCACHE on macOS) so you measure the device, not memcpy. Without it,
// the second run of every benchmark reads from RAM. Try both: the gap IS
// curriculum/04-page-cache.md.

#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "fs/file_descriptor.h"
#include "io/io_context.h"
#include "util/random.h"

namespace strata {
namespace {

constexpr std::size_t kBlock = 4096;
constexpr std::size_t kFileSize = 256u << 20;

struct AlignedFree {
  void operator()(char* p) const { std::free(p); }
};
using AlignedBuffer = std::unique_ptr<char[], AlignedFree>;

AlignedBuffer AllocAligned(std::size_t bytes) {
  return AlignedBuffer(static_cast<char*>(std::aligned_alloc(kBlock, bytes)));
}

// Creates the data file once per process.
const std::filesystem::path& DataFile() {
  static const std::filesystem::path path = [] {
    const char* d = std::getenv("STRATA_BENCH_DIR");
    auto p = (d != nullptr ? std::filesystem::path(d) : std::filesystem::temp_directory_path()) /
             "strata_bench_async_read.dat";
    if (!std::filesystem::exists(p) || std::filesystem::file_size(p) != kFileSize) {
      FileDescriptor fd(::open(p.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644));
      std::string chunk(1 << 20, '\0');
      Random rnd(1);
      for (auto& c : chunk) c = static_cast<char>(rnd.Uniform(256));
      for (std::size_t off = 0; off < kFileSize; off += chunk.size()) {
        if (::write(fd.get(), chunk.data(), chunk.size()) < 0) std::abort();
      }
      static_cast<void>(fd.Close());
    }
    return p;
  }();
  return path;
}

FileDescriptor OpenForBench() {
  const bool direct = std::getenv("STRATA_BENCH_DIRECT") != nullptr;
  int flags = O_RDONLY;
#if defined(__linux__)
  if (direct) flags |= O_DIRECT;
#endif
  FileDescriptor fd(::open(DataFile().c_str(), flags));
#if defined(__APPLE__)
  if (direct && fd) ::fcntl(fd.get(), F_NOCACHE, 1);
#endif
  return fd;
}

Task<Result<std::size_t>> ReadOne(IoContext& io, int fd, std::uint64_t off, std::span<char> buf) {
  co_return co_await io.Read(fd, off, buf);
}

Task<std::size_t> Batch(IoContext& io, int fd, Random& rnd, std::span<char> buffers,
                        std::size_t depth) {
  std::vector<Task<Result<std::size_t>>> reads;
  reads.reserve(depth);
  for (std::size_t i = 0; i < depth; i++) {
    const std::uint64_t off = rnd.Uniform(kFileSize / kBlock) * kBlock;
    reads.push_back(ReadOne(io, fd, off, buffers.subspan(i * kBlock, kBlock)));
  }
  std::size_t bytes = 0;
  for (auto& r : co_await WhenAll(std::move(reads))) bytes += r ? *r : 0;
  co_return bytes;
}

void RunWith(benchmark::State& state, IoContext& io) {
  const auto depth = static_cast<std::size_t>(state.range(0));
  FileDescriptor fd = OpenForBench();
  if (!fd) {
    state.SkipWithError("open failed");
    return;
  }
  auto buffers = AllocAligned(depth * kBlock);
  Random rnd(42);
  std::size_t bytes = 0;
  for (auto _ : state)
    bytes += io.RunUntilComplete(Batch(io, fd.get(), rnd, {buffers.get(), depth * kBlock}, depth));
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(depth));
  state.SetBytesProcessed(static_cast<std::int64_t>(bytes));
}

void BM_Blocking(benchmark::State& state) {
  auto io = NewBlockingIoContext();
  RunWith(state, *io);
}
BENCHMARK(BM_Blocking)->Arg(1)->Arg(4)->Arg(16)->Arg(64)->UseRealTime();

#if defined(STRATA_HAVE_IO_URING)
void BM_IoUring(benchmark::State& state) {
  auto io = NewIoUringContext(256);
  if (!io) {
    state.SkipWithError(io.error().ToString());
    return;
  }
  RunWith(state, **io);
}
BENCHMARK(BM_IoUring)->Arg(1)->Arg(4)->Arg(16)->Arg(64)->UseRealTime();
#endif

}  // namespace
}  // namespace strata

BENCHMARK_MAIN();
