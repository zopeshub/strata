#pragma once

// Endian-neutral binary encoding (M1). Every on-disk format in strata is
// built from these primitives, so they must be exact:
//
//   * Fixed-width integers are LITTLE-endian regardless of host byte order.
//   * Varints are LEB128-style: 7 bits per byte, low groups first, high bit
//     set on every byte except the last. uint32 takes at most 5 bytes,
//     uint64 at most 10.
//   * A length-prefixed string is varint32(length) followed by the bytes.
//
// Parsing functions consume from the front of `input` on success. On failure
// they return nullopt and leave `input` in an unspecified state (callers
// treat any failure as corruption and stop).
//
// C++ focus for this file (docs/roadmap.md, M1): std::endian and
// std::byteswap / std::bit_cast vs. memcpy vs. reinterpret_cast (which of
// these is UB?), constexpr, [[nodiscard]], and not allocating on hot paths.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace strata {

inline constexpr int kMaxVarint32Bytes = 5;
inline constexpr int kMaxVarint64Bytes = 10;

// Writes exactly 4/8 bytes to `dst`.
void EncodeFixed32(char* dst, std::uint32_t value);
void EncodeFixed64(char* dst, std::uint64_t value);

// Reads exactly 4/8 bytes from `src`.
[[nodiscard]] std::uint32_t DecodeFixed32(const char* src);
[[nodiscard]] std::uint64_t DecodeFixed64(const char* src);

void PutFixed32(std::string& dst, std::uint32_t value);
void PutFixed64(std::string& dst, std::uint64_t value);
void PutVarint32(std::string& dst, std::uint32_t value);
void PutVarint64(std::string& dst, std::uint64_t value);
void PutLengthPrefixed(std::string& dst, std::string_view value);

// Number of bytes the varint encoding of `value` takes (1..10).
[[nodiscard]] int VarintLength(std::uint64_t value);

// Writes the varint encoding of `value` starting at `dst`; returns a pointer
// one past the last byte written. `dst` must have room for kMaxVarint*Bytes.
char* EncodeVarint32(char* dst, std::uint32_t value);
char* EncodeVarint64(char* dst, std::uint64_t value);

// Rejects truncated input and encodings that don't fit the type: a uint32
// varint with a 6th byte, or whose 5th byte is > 0x0f, is malformed; a
// uint64 varint with an 11th byte, or whose 10th byte is > 0x01, likewise.
[[nodiscard]] std::optional<std::uint32_t> GetVarint32(std::string_view& input);
[[nodiscard]] std::optional<std::uint64_t> GetVarint64(std::string_view& input);
[[nodiscard]] std::optional<std::uint32_t> GetFixed32(std::string_view& input);
[[nodiscard]] std::optional<std::uint64_t> GetFixed64(std::string_view& input);
// The returned view points into the original input buffer.
[[nodiscard]] std::optional<std::string_view> GetLengthPrefixed(std::string_view& input);

}  // namespace strata
