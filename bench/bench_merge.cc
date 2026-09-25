// M5. Merging N sorted children. A linear scan of children per Next() is
// O(N); a heap is O(log N). Where is the crossover on your machine? (L0 can
// have ~12 files plus memtables, so N is small: does the heap even win?)

#include <format>
#include <memory>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/dbformat.h"
#include "table/block.h"
#include "table/block_builder.h"
#include "table/merging_iterator.h"

namespace strata {
namespace {

constexpr int kTotal = 100'000;

void BM_MergeScan(benchmark::State& state) {
  const int children = static_cast<int>(state.range(0));
  // Child c holds keys i with i % children == c: maximally interleaved.
  std::vector<std::unique_ptr<Block>> blocks;
  for (int c = 0; c < children; c++) {
    BlockBuilder b(16);
    for (int i = c; i < kTotal; i += children) b.Add(std::format("{:010}", i), "v");
    blocks.push_back(std::make_unique<Block>(std::string(b.Finish())));
  }
  for (auto _ : state) {
    std::vector<std::unique_ptr<Iterator>> its;
    for (auto& blk : blocks) its.push_back(blk->NewIterator(*BytewiseComparator()));
    auto merged = NewMergingIterator(*BytewiseComparator(), std::move(its));
    int n = 0;
    for (merged->SeekToFirst(); merged->Valid(); merged->Next()) n++;
    benchmark::DoNotOptimize(n);
  }
  state.SetItemsProcessed(state.iterations() * kTotal);
}
BENCHMARK(BM_MergeScan)->RangeMultiplier(2)->Range(1, 64);

}  // namespace
}  // namespace strata

BENCHMARK_MAIN();
