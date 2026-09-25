#pragma once

// Bloom filter (M4). One filter per SSTable, over USER keys, so a point
// lookup for an absent key usually costs zero data-block reads.
//
// Filter layout: bit array (ceil(n * bits_per_key / 8) bytes, minimum 8)
// followed by one byte holding k, the number of probes. Storing k in the
// filter lets readers handle filters built with different settings.
//
// Use double hashing (Kirsch & Mitzenmacher): h = Hash(key, seed),
// delta = rotate(h, 15); probe i uses h + i*delta. k = bits_per_key * ln 2,
// clamped to [1, 30]. The test demands a false-positive rate under 2% at 10
// bits/key; the theory says ~0.8%. If you're at 1.5%, something's off.

#include <span>
#include <string>
#include <string_view>

namespace strata {

class BloomFilterPolicy {
 public:
  explicit BloomFilterPolicy(int bits_per_key);

  // Appends a filter for `keys` to `dst`.
  void CreateFilter(std::span<const std::string_view> keys, std::string& dst) const;

  // False means "definitely absent". A malformed/short filter must return
  // true (it's always safe to say "maybe").
  [[nodiscard]] bool KeyMayMatch(std::string_view key, std::string_view filter) const;

 private:
  // TODO(M4): bits_per_key_, k_.
};

}  // namespace strata
