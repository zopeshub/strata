#include "table/bloom.h"

#include "strata/todo.h"
#include "util/hash.h"

namespace strata {

BloomFilterPolicy::BloomFilterPolicy(int /*bits_per_key*/) {
  STRATA_TODO("M4", "BloomFilterPolicy constructor (compute k)");
}

void BloomFilterPolicy::CreateFilter(std::span<const std::string_view> /*keys*/,
                                     std::string& /*dst*/) const {
  STRATA_TODO("M4", "BloomFilterPolicy::CreateFilter");
}

bool BloomFilterPolicy::KeyMayMatch(std::string_view /*key*/, std::string_view /*filter*/) const {
  STRATA_TODO("M4", "BloomFilterPolicy::KeyMayMatch");
}

}  // namespace strata
