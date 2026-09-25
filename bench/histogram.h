#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace strata::bench {

// Exact latency recorder: keeps every sample (8 bytes each; 10M ops = 80 MB,
// fine for a benchmark). Exact percentiles beat bucketed ones for the tail.
class Histogram {
 public:
  void Reserve(std::size_t n) { samples_.reserve(n); }
  void Add(std::uint64_t nanos) { samples_.push_back(nanos); }
  void Merge(const Histogram& other);

  [[nodiscard]] std::size_t count() const { return samples_.size(); }
  // Sorts on first call after Add/Merge.
  [[nodiscard]] double Percentile(double p);
  [[nodiscard]] double Mean() const;
  [[nodiscard]] std::uint64_t Max();
  // "p50 ... p99.9 ... max" in microseconds.
  [[nodiscard]] std::string Summary();

 private:
  void Sort();
  std::vector<std::uint64_t> samples_;
  bool sorted_ = true;
};

}  // namespace strata::bench
