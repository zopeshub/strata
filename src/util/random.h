#pragma once

// Small, fast, deterministic PRNG (splitmix64) for tests, benchmarks, and
// skiplist heights. Deterministic seeds make failing randomized tests
// reproducible: every randomized test prints its seed.

#include <cstdint>

namespace strata {

class Random {
 public:
  explicit constexpr Random(std::uint64_t seed) noexcept : state_(seed) {}

  constexpr std::uint64_t Next() noexcept {
    std::uint64_t z = (state_ += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
  }

  // Uniform in [0, n). REQUIRES: n > 0. (Slight modulo bias is fine here.)
  constexpr std::uint64_t Uniform(std::uint64_t n) noexcept { return Next() % n; }

  // True with probability 1/n.
  constexpr bool OneIn(std::uint64_t n) noexcept { return Uniform(n) == 0; }

  // Picks a "base" uniformly from [0, max_log], then returns a value uniform
  // in [0, 2^base). Favors small numbers with an exponential bias; good for
  // generating record sizes that exercise both tiny and huge cases.
  constexpr std::uint64_t Skewed(int max_log) noexcept {
    return Uniform(std::uint64_t{1} << Uniform(static_cast<std::uint64_t>(max_log) + 1));
  }

 private:
  std::uint64_t state_;
};

}  // namespace strata
