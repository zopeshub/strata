// M2 (std::map memtable) vs M3 (arena + skiplist). Run this before AND
// after M3 and keep both outputs in bench/results/: that before/after is
// part of the M3 demo.
//
// Questions: where does the std::map version spend its time (perf/Instruments:
// malloc? pointer chasing?) How does Get scale with reader threads in each
// version, and why does one of them need a lock and the other not?

#include <format>
#include <memory>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "db/dbformat.h"
#include "db/memtable.h"
#include "util/random.h"

namespace strata {
namespace {

std::string Key(std::uint64_t i) { return std::format("{:016}", i); }

void BM_MemTableAddSequential(benchmark::State& state) {
  const InternalKeyComparator icmp;
  const std::string value(100, 'v');
  for (auto _ : state) {
    state.PauseTiming();
    auto mem = std::make_unique<MemTable>(icmp);
    state.ResumeTiming();
    for (std::uint64_t i = 0; i < 10'000; i++) mem->Add(i + 1, ValueType::kValue, Key(i), value);
    state.PauseTiming();
    mem.reset();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations() * 10'000);
}
BENCHMARK(BM_MemTableAddSequential);

void BM_MemTableAddRandom(benchmark::State& state) {
  const InternalKeyComparator icmp;
  const std::string value(100, 'v');
  Random rnd(1);
  for (auto _ : state) {
    state.PauseTiming();
    auto mem = std::make_unique<MemTable>(icmp);
    state.ResumeTiming();
    for (std::uint64_t i = 0; i < 10'000; i++)
      mem->Add(i + 1, ValueType::kValue, Key(rnd.Next()), value);
    state.PauseTiming();
    mem.reset();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(state.iterations() * 10'000);
}
BENCHMARK(BM_MemTableAddRandom);

// Shared, pre-filled memtable for the concurrent read benchmark.
MemTable& Filled() {
  static const InternalKeyComparator icmp;
  static MemTable* mem = [] {
    auto* m = new MemTable(icmp);
    for (std::uint64_t i = 0; i < 200'000; i++)
      m->Add(i + 1, ValueType::kValue, Key(i * 2), "value");
    return m;
  }();
  return *mem;
}

void BM_MemTableGet(benchmark::State& state) {
  MemTable& mem = Filled();
  Random rnd(static_cast<std::uint64_t>(state.thread_index()) + 1);
  std::uint64_t hits = 0;
  for (auto _ : state) {
    const LookupKey lk(Key(rnd.Uniform(400'000)), kMaxSequenceNumber);
    hits += std::holds_alternative<std::string>(mem.Get(lk)) ? 1 : 0;
  }
  benchmark::DoNotOptimize(hits);
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MemTableGet)->ThreadRange(1, 8)->UseRealTime();

void BM_MemTableScan(benchmark::State& state) {
  MemTable& mem = Filled();
  for (auto _ : state) {
    auto it = mem.NewIterator();
    std::size_t n = 0;
    for (it->SeekToFirst(); it->Valid(); it->Next()) n++;
    benchmark::DoNotOptimize(n);
  }
  state.SetItemsProcessed(state.iterations() * 200'000);
}
BENCHMARK(BM_MemTableScan);

}  // namespace
}  // namespace strata

BENCHMARK_MAIN();
