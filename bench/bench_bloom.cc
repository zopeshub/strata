// M4. Reports the MEASURED false-positive rate as a counter. Compare it to
// the theory: FPR ~= (1 - e^(-k*n/m))^k with m/n = bits_per_key and
// k = round(bits_per_key * ln 2). If you're far off, your double hashing
// is probably broken (the tests only check an upper bound).

#include <format>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include "table/bloom.h"

namespace strata {
namespace {

std::vector<std::string> Keys(std::size_t n, std::uint64_t salt) {
  std::vector<std::string> keys;
  keys.reserve(n);
  for (std::size_t i = 0; i < n; i++) keys.push_back(std::format("key-{}-{:012}", salt, i));
  return keys;
}

void BM_BloomBuild(benchmark::State& state) {
  const BloomFilterPolicy policy(static_cast<int>(state.range(0)));
  const auto keys = Keys(10'000, 0);
  const std::vector<std::string_view> views(keys.begin(), keys.end());
  std::string filter;
  for (auto _ : state) {
    filter.clear();
    policy.CreateFilter(views, filter);
    benchmark::DoNotOptimize(filter.data());
  }
  state.SetItemsProcessed(state.iterations() * 10'000);
  state.counters["bytes/key"] = static_cast<double>(filter.size()) / 10'000.0;
}
BENCHMARK(BM_BloomBuild)->Arg(6)->Arg(10)->Arg(16);

void BM_BloomProbeMissing(benchmark::State& state) {
  const BloomFilterPolicy policy(static_cast<int>(state.range(0)));
  const auto keys = Keys(10'000, 0);
  const std::vector<std::string_view> views(keys.begin(), keys.end());
  std::string filter;
  policy.CreateFilter(views, filter);
  const auto probes = Keys(10'000, 1);
  std::uint64_t positives = 0;
  std::uint64_t total = 0;
  for (auto _ : state) {
    for (const auto& p : probes) positives += policy.KeyMayMatch(p, filter) ? 1 : 0;
    total += probes.size();
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(total));
  state.counters["FPR%"] = 100.0 * static_cast<double>(positives) /
                           static_cast<double>(std::max<std::uint64_t>(total, 1));
}
BENCHMARK(BM_BloomProbeMissing)->Arg(6)->Arg(10)->Arg(16);

}  // namespace
}  // namespace strata

BENCHMARK_MAIN();
