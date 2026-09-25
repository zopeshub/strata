// M4. The restart interval trades space (prefix compression) for seek time
// (linear scan between restarts). Sweep it and put the curve in your ADR:
// block size vs seek latency for intervals 1, 4, 16, 64.

#include <format>
#include <string>

#include <benchmark/benchmark.h>

#include "db/dbformat.h"
#include "table/block.h"
#include "table/block_builder.h"
#include "util/random.h"

namespace strata {
namespace {

constexpr int kEntries = 4000;

std::string Key(int i) { return std::format("user:{:010}:profile", i); }

std::string BuildBlock(int restart_interval) {
  BlockBuilder b(restart_interval);
  for (int i = 0; i < kEntries; i++) b.Add(Key(i), "v");
  return std::string(b.Finish());
}

void BM_BlockBuild(benchmark::State& state) {
  const int interval = static_cast<int>(state.range(0));
  std::size_t size = 0;
  for (auto _ : state) {
    BlockBuilder b(interval);
    for (int i = 0; i < kEntries; i++) b.Add(Key(i), "v");
    size = b.Finish().size();
  }
  state.SetItemsProcessed(state.iterations() * kEntries);
  state.counters["block_bytes"] = static_cast<double>(size);
}
BENCHMARK(BM_BlockBuild)->Arg(1)->Arg(4)->Arg(16)->Arg(64);

void BM_BlockSeek(benchmark::State& state) {
  const Block block(BuildBlock(static_cast<int>(state.range(0))));
  auto it = block.NewIterator(*BytewiseComparator());
  Random rnd(3);
  for (auto _ : state) {
    it->Seek(Key(static_cast<int>(rnd.Uniform(kEntries))));
    benchmark::DoNotOptimize(it->Valid());
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BlockSeek)->Arg(1)->Arg(4)->Arg(16)->Arg(64);

void BM_BlockScan(benchmark::State& state) {
  const Block block(BuildBlock(16));
  auto it = block.NewIterator(*BytewiseComparator());
  for (auto _ : state) {
    int n = 0;
    for (it->SeekToFirst(); it->Valid(); it->Next()) n++;
    benchmark::DoNotOptimize(n);
  }
  state.SetItemsProcessed(state.iterations() * kEntries);
}
BENCHMARK(BM_BlockScan);

}  // namespace
}  // namespace strata

BENCHMARK_MAIN();
