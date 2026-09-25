#include "util/hash.h"

#include <bit>
#include <cstring>

namespace strata {

namespace {
std::uint32_t LoadLE32(const char* p) {
  std::uint32_t v;
  std::memcpy(&v, p, sizeof(v));
  if constexpr (std::endian::native == std::endian::big) v = std::byteswap(v);
  return v;
}
}  // namespace

std::uint32_t Hash(std::string_view data, std::uint32_t seed) {
  constexpr std::uint32_t m = 0xc6a4a793;
  constexpr std::uint32_t r = 24;
  const char* p = data.data();
  const char* limit = p + data.size();
  std::uint32_t h = seed ^ (static_cast<std::uint32_t>(data.size()) * m);

  while (limit - p >= 4) {
    h += LoadLE32(p);
    p += 4;
    h *= m;
    h ^= (h >> 16);
  }

  switch (limit - p) {
    case 3:
      h += static_cast<std::uint32_t>(static_cast<unsigned char>(p[2])) << 16;
      [[fallthrough]];
    case 2: h += static_cast<std::uint32_t>(static_cast<unsigned char>(p[1])) << 8; [[fallthrough]];
    case 1:
      h += static_cast<std::uint32_t>(static_cast<unsigned char>(p[0]));
      h *= m;
      h ^= (h >> r);
      break;
    default: break;
  }
  return h;
}

}  // namespace strata
