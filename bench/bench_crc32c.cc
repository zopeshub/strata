// M1. Your table-driven CRC32C should reach roughly 0.5-1.5 GB/s. The
// stretch goal (hardware CRC: SSE4.2 _mm_crc32_u64 on x86, __crc32cd on
// ARMv8, behind a runtime/compile-time check) should reach 10+ GB/s. Is the
// WAL's CRC ever the bottleneck next to an fsync? Compute it from these
// numbers and bench_fsync's.

#include <string>

#include <benchmark/benchmark.h>

#include "util/crc32c.h"
#include "util/random.h"

namespace strata {
namespace {

void BM_Crc32c(benchmark::State& state) {
  Random rnd(7);
  std::string data(static_cast<std::size_t>(state.range(0)), '\0');
  for (auto& c : data) c = static_cast<char>(rnd.Uniform(256));
  for (auto _ : state) benchmark::DoNotOptimize(crc32c::Value(data));
  state.SetBytesProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_Crc32c)->RangeMultiplier(8)->Range(64, 1 << 20);

}  // namespace
}  // namespace strata

BENCHMARK_MAIN();
