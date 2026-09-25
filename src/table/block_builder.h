#pragma once

// Builds a block of sorted key/value entries with prefix compression. (M4)
//
// Entry encoding:
//   shared:varint32 | non_shared:varint32 | value_len:varint32 |
//   key_delta[non_shared] | value[value_len]
// where `shared` = length of the common prefix with the previous key.
// Every `restart_interval` entries, a RESTART point stores the full key
// (shared = 0). The block ends with:
//   restarts[num_restarts]:fixed32 (offsets of restart entries) | num_restarts:fixed32
// Readers binary-search the restart array, then scan linearly.
// An empty block still has one restart point (offset 0).

#include <cstddef>
#include <string>
#include <string_view>

namespace strata {

class BlockBuilder {
 public:
  explicit BlockBuilder(int restart_interval);

  BlockBuilder(const BlockBuilder&) = delete;
  BlockBuilder& operator=(const BlockBuilder&) = delete;

  // Back to the empty state (reusing allocated memory).
  void Reset();

  // REQUIRES: Finish() not called since the last Reset(); key is greater
  // than every previously added key (caller's comparator).
  void Add(std::string_view key, std::string_view value);

  // Appends the restart array. The view is valid until Reset() or destruction.
  std::string_view Finish();

  // Size of the block Finish() would produce now.
  [[nodiscard]] std::size_t CurrentSizeEstimate() const;

  [[nodiscard]] bool empty() const;

 private:
  // TODO(M4): buffer_, restarts_, counter_, finished_, last_key_, interval.
};

}  // namespace strata
