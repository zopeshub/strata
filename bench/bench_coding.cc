// M1. Questions to answer with this benchmark (write them in your ADR):
//   * How much slower is GetVarint64 than DecodeFixed64 per value? Why?
//   * Does your decoder's fast path (1-byte varints) show up for small values?
//   * What does `benchmark::DoNotOptimize` protect against here?

#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "util/coding.h"
#include "util/random.h"

namespace strata {
namespace {

std::vector<std::uint64_t> Values(std::int64_t max_bits) {
  Random rnd(1);
  std::vector<std::uint64_t> v(4096);
  for (auto& x : v) x = rnd.Next() >> (64 - rnd.Uniform(static_cast<std::uint64_t>(max_bits)) - 1);
  return v;
}

void BM_PutVarint64(benchmark::State& state) {
  const auto values = Values(state.range(0));
  std::string buf;
  for (auto _ : state) {
    buf.clear();
    for (auto x : values) PutVarint64(buf, x);
    benchmark::DoNotOptimize(buf.data());
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(values.size()));
}
BENCHMARK(BM_PutVarint64)->Arg(7)->Arg(21)->Arg(64);

void BM_GetVarint64(benchmark::State& state) {
  const auto values = Values(state.range(0));
  std::string buf;
  for (auto x : values) PutVarint64(buf, x);
  for (auto _ : state) {
    std::string_view in = buf;
    std::uint64_t sum = 0;
    while (auto v = GetVarint64(in)) sum += *v;
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(values.size()));
}
BENCHMARK(BM_GetVarint64)->Arg(7)->Arg(21)->Arg(64);

void BM_DecodeFixed64(benchmark::State& state) {
  const auto values = Values(64);
  std::string buf;
  for (auto x : values) PutFixed64(buf, x);
  for (auto _ : state) {
    std::uint64_t sum = 0;
    for (std::size_t off = 0; off < buf.size(); off += 8) sum += DecodeFixed64(buf.data() + off);
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(values.size()));
}
BENCHMARK(BM_DecodeFixed64);

}  // namespace
}  // namespace strata

BENCHMARK_MAIN();
