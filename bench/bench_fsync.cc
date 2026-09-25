// M1 + curriculum/07-fsync-semantics.md. The most important numbers in the
// whole project: what does durability cost on YOUR machine?
//
//   STRATA_BENCH_DIR=/path/on/the/disk/you/care/about ./bench_fsync
//
// Defaults to /tmp. In the Lima VM, use a path on the VM's own disk, NOT
// the virtiofs mount of your Mac home directory.
//
// Variants:
//   Append/<method>/<bytes>     write at EOF then sync: file size changes,
//                               so metadata (the inode's size) must be synced too
//   Overwrite/<method>/<bytes>  rewrite a preallocated region: size unchanged,
//                               fdatasync can skip the inode (LevelDB/RocksDB
//                               WAL-preallocation trick)
// Questions: on macOS, how many times slower is F_FULLFSYNC than fsync, and
// what does that tell you about what fsync does there? On Linux, does
// fdatasync beat fsync for Append? For Overwrite? Why?

#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include <benchmark/benchmark.h>

#include "fs/file_descriptor.h"
#include "strata/options.h"

namespace strata {
namespace {

std::filesystem::path BenchDir() {
  const char* d = std::getenv("STRATA_BENCH_DIR");
  return d != nullptr ? std::filesystem::path(d) : std::filesystem::temp_directory_path();
}

const char* MethodName(SyncMethod m) {
  switch (m) {
    case SyncMethod::kFsync: return "fsync";
    case SyncMethod::kFdatasync: return "fdatasync";
    case SyncMethod::kFullFsync: return "fullfsync";
    case SyncMethod::kBarrierFsync: return "barrierfsync";
  }
  return "?";
}

void Run(benchmark::State& state, bool overwrite) {
  const auto method = static_cast<SyncMethod>(state.range(0));
  const auto bytes = static_cast<std::size_t>(state.range(1));
  const auto path = BenchDir() / ("strata_bench_fsync_" + std::to_string(::getpid()));
  FileDescriptor fd(::open(path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644));
  if (!fd) {
    state.SkipWithError("open failed");
    return;
  }
  const std::string buf(bytes, 'w');
  constexpr std::size_t kRegion = 64u << 20;
  if (overwrite) {
    // Preallocate by writing (not ftruncate: holes don't count) and sync.
    const std::string chunk(1 << 20, 'p');
    for (std::size_t off = 0; off < kRegion; off += chunk.size()) {
      if (::pwrite(fd.get(), chunk.data(), chunk.size(), static_cast<off_t>(off)) < 0) {
        state.SkipWithError("prealloc failed");
        return;
      }
    }
    static_cast<void>(SyncFd(fd.get(), SyncMethod::kFsync, "prealloc"));
  }
  std::size_t offset = 0;
  for (auto _ : state) {
    if (overwrite && offset + bytes > kRegion) offset = 0;
    if (::pwrite(fd.get(), buf.data(), bytes, static_cast<off_t>(offset)) !=
        static_cast<ssize_t>(bytes)) {
      state.SkipWithError("pwrite failed");
      break;
    }
    offset += bytes;
    if (Status s = SyncFd(fd.get(), method, "bench"); !s.ok()) {
      state.SkipWithError(s.ToString());
      break;
    }
  }
  state.SetLabel(MethodName(method));
  state.SetItemsProcessed(state.iterations());
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(bytes));
  static_cast<void>(fd.Close());
  std::filesystem::remove(path);
}

void BM_Append(benchmark::State& state) { Run(state, false); }
void BM_Overwrite(benchmark::State& state) { Run(state, true); }

void Args(benchmark::internal::Benchmark* b) {
  for (auto m : {SyncMethod::kFsync, SyncMethod::kFdatasync, SyncMethod::kFullFsync,
                 SyncMethod::kBarrierFsync}) {
    for (std::int64_t bytes : {512, 4096, 65536, 1 << 20})
      b->Args({static_cast<std::int64_t>(m), bytes});
  }
  b->ArgNames({"method", "bytes"})->UseRealTime()->MinTime(0.5);
}

BENCHMARK(BM_Append)->Apply(Args);
BENCHMARK(BM_Overwrite)->Apply(Args);

}  // namespace
}  // namespace strata

BENCHMARK_MAIN();
