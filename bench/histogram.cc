#include "histogram.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>

namespace strata::bench {

void Histogram::Merge(const Histogram& other) {
  samples_.insert(samples_.end(), other.samples_.begin(), other.samples_.end());
  sorted_ = false;
}

void Histogram::Sort() {
  if (!sorted_) {
    std::ranges::sort(samples_);
    sorted_ = true;
  }
}

double Histogram::Percentile(double p) {
  if (samples_.empty()) return 0;
  Sort();
  const double rank = p / 100.0 * static_cast<double>(samples_.size() - 1);
  const auto lo = static_cast<std::size_t>(std::floor(rank));
  const auto hi = static_cast<std::size_t>(std::ceil(rank));
  const double frac = rank - static_cast<double>(lo);
  return static_cast<double>(samples_[lo]) * (1 - frac) + static_cast<double>(samples_[hi]) * frac;
}

double Histogram::Mean() const {
  if (samples_.empty()) return 0;
  const long double sum = std::accumulate(samples_.begin(), samples_.end(), 0.0L);
  return static_cast<double>(sum / static_cast<long double>(samples_.size()));
}

std::uint64_t Histogram::Max() {
  if (samples_.empty()) return 0;
  Sort();
  return samples_.back();
}

std::string Histogram::Summary() {
  auto us = [](double ns) { return ns / 1000.0; };
  return std::format(
      "    latency us: p50 {:.2f}  p90 {:.2f}  p99 {:.2f}  p99.9 {:.2f}  max {:.2f}  (n={})",
      us(Percentile(50)), us(Percentile(90)), us(Percentile(99)), us(Percentile(99.9)),
      us(static_cast<double>(Max())), count());
}

}  // namespace strata::bench
