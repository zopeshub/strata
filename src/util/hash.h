#pragma once

// A fast non-cryptographic 32-bit hash (Murmur-style). Used by the bloom
// filter and the table cache. Provided: this is plumbing, not the lesson.

#include <cstdint>
#include <string_view>

namespace strata {

[[nodiscard]] std::uint32_t Hash(std::string_view data, std::uint32_t seed);

}  // namespace strata
