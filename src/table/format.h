#pragma once

// SSTable physical format (M4). Full layout and rationale: docs/design.md §4.
//
//   [data block 0][trailer] ... [data block N-1][trailer]
//   [filter block][trailer]            bloom filter over all user keys
//   [properties block][trailer]        TableProperties
//   [metaindex block][trailer]         "filter.bloom" -> handle, "strata.properties" -> handle
//   [index block][trailer]             separator key -> data block handle
//   [footer: 48 bytes]                 metaindex handle, index handle, padding, magic
//
//   trailer := compression_type:u8 | masked_crc32c(block_contents + type):fixed32

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "db/dbformat.h"
#include "strata/file_system.h"
#include "strata/status.h"

namespace strata {

struct TableOptions {
  // Orders keys in data and index blocks. For DB tables, an InternalKeyComparator.
  const Comparator* comparator = BytewiseComparator();
  std::size_t block_size = 4u << 10;
  int block_restart_interval = 16;
  // 0 disables the filter block. Filters are built on user keys, so this
  // requires keys to be internal keys (ExtractUserKey is applied).
  int bloom_bits_per_key = 10;
};

// Location of a block within a file.
struct BlockHandle {
  std::uint64_t offset = 0;
  std::uint64_t size = 0;  // excludes the trailer

  static constexpr std::size_t kMaxEncodedLength = 10 + 10;

  void EncodeTo(std::string& dst) const;  // varint64 offset, varint64 size
  [[nodiscard]] static std::optional<BlockHandle> DecodeFrom(std::string_view& input);

  friend bool operator==(const BlockHandle&, const BlockHandle&) = default;
};

// Fixed-size footer at the very end of every table file.
struct Footer {
  BlockHandle metaindex_handle;
  BlockHandle index_handle;

  // Two handles padded to 2*kMaxEncodedLength, then the 8-byte magic.
  static constexpr std::size_t kEncodedLength = 2 * BlockHandle::kMaxEncodedLength + 8;

  void EncodeTo(std::string& dst) const;  // appends exactly kEncodedLength bytes
  // `input` must be exactly the last kEncodedLength bytes of the file.
  // Corruption on bad magic or undecodable handles.
  [[nodiscard]] static Result<Footer> DecodeFrom(std::string_view input);
};

// "strata01" read as a little-endian fixed64.
inline constexpr std::uint64_t kTableMagicNumber = 0x3130617461727473ULL;

enum class CompressionType : std::uint8_t { kNone = 0 };

// compression type (1) + masked crc32c (4)
inline constexpr std::size_t kBlockTrailerSize = 5;

// Reads the block at `handle` and its trailer. If verify_checksums, checks
// the CRC and returns Corruption on mismatch. Returns the block contents
// without the trailer.
Result<std::string> ReadBlock(const RandomAccessFile& file, const BlockHandle& handle,
                              bool verify_checksums);

}  // namespace strata
