#pragma once

// Write-ahead log physical format (M2). This is a SPEC: tests check it at
// the byte level. Full rationale in docs/design.md §3.
//
// The log is a sequence of 32 KiB blocks. Each block holds physical records:
//
//   +---------+-----------+-----------+--- ... ---+
//   | CRC (4) | Length(2) | Type (1)  | Payload   |
//   +---------+-----------+-----------+--- ... ---+
//
//   CRC    = crc32c::Mask(crc32c(type_byte + payload)), fixed32 little-endian
//   Length = payload length, fixed16 little-endian (<= kBlockSize - kHeaderSize)
//   Type   = RecordType below
//
// A logical record (one WriteBatch) that doesn't fit in the rest of the
// current block is split into FIRST, MIDDLE*, LAST fragments. A record that
// fits entirely is FULL. If fewer than kHeaderSize bytes remain in a block,
// they are zero-filled (a "trailer") and writing continues in the next block.
// A zero-length FIRST fragment is legal (it happens when exactly
// kHeaderSize bytes remain).

#include <cstddef>
#include <cstdint>

namespace strata::log {

enum class RecordType : std::uint8_t {
  kZero = 0,  // reserved for preallocated/zeroed file regions
  kFull = 1,
  kFirst = 2,
  kMiddle = 3,
  kLast = 4,
};

inline constexpr std::uint8_t kMaxRecordType = static_cast<std::uint8_t>(RecordType::kLast);

inline constexpr std::size_t kBlockSize = 32768;

// crc (4) + length (2) + type (1)
inline constexpr std::size_t kHeaderSize = 4 + 2 + 1;

}  // namespace strata::log
