#pragma once

// CRC-32C (Castagnoli, polynomial 0x1EDC6F41, reflected 0x82F63B78), the
// checksum used by iSCSI, ext4 metadata, RocksDB/LevelDB, and strata's WAL
// and SSTable blocks. (M1)
//
// Why CRC32C and not CRC32 (zlib)? Hardware support: SSE4.2 `crc32` and
// ARMv8 `__crc32cd` compute it at >10 GB/s. Start with a table-driven
// software version; the hardware path is an M1 stretch goal.

#include <cstdint>
#include <string_view>

namespace strata::crc32c {

// Returns the crc32c of concat(A, data) where init_crc is the crc32c of A.
[[nodiscard]] std::uint32_t Extend(std::uint32_t init_crc, std::string_view data);

[[nodiscard]] inline std::uint32_t Value(std::string_view data) { return Extend(0, data); }

// Storing the CRC of a string that itself contains embedded CRCs is
// problematic (the CRC of data+its CRC is a constant). So stored CRCs are
// "masked": rotate right by 15 bits and add a constant (0xa282ead8).
[[nodiscard]] std::uint32_t Mask(std::uint32_t crc);
[[nodiscard]] std::uint32_t Unmask(std::uint32_t masked_crc);

}  // namespace strata::crc32c
