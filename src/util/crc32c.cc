#include "util/crc32c.h"

#include "strata/todo.h"

namespace strata::crc32c {

// TODO(M1, C++): Generate the 256-entry lookup table at COMPILE TIME:
//
//   constexpr std::array<std::uint32_t, 256> kTable = MakeTable();
//
// and prove it with a static_assert on a known entry (kTable[1] must be
// 0xf26b8303). No hand-pasted tables. Then implement the byte-at-a-time
// loop. Remember the standard pre/post conditioning (~crc on entry and exit)
// and make sure Extend() composes: Extend(Value(a), b) == Value(a + b).
//
// Stretch (M1+): slicing-by-8 (8 tables) and/or hardware CRC via
// <arm_acle.h> __crc32cd / <nmmintrin.h> _mm_crc32_u64, selected at compile
// time with `if constexpr` or at run time. Measure with bench/bench_crc32c.

std::uint32_t Extend(std::uint32_t /*init_crc*/, std::string_view /*data*/) {
  STRATA_TODO("M1", "crc32c::Extend");
}

std::uint32_t Mask(std::uint32_t /*crc*/) { STRATA_TODO("M1", "crc32c::Mask"); }

std::uint32_t Unmask(std::uint32_t /*masked_crc*/) { STRATA_TODO("M1", "crc32c::Unmask"); }

}  // namespace strata::crc32c
