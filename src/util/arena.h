#pragma once

// Bump-pointer arena for memtable entries and skiplist nodes. (M3)
//
// Why an arena? A memtable does millions of tiny allocations that all die at
// the same moment (when the memtable is flushed and dropped). Paying
// malloc/free per entry is wasted work and fragments the heap; an arena turns
// that into a pointer bump and one bulk free.
//
// Contract:
//   * Allocate()/AllocateAligned() are called by ONE thread at a time (the
//     memtable writer). MemoryUsage() may be called concurrently from any
//     thread (the DB reads it to decide when to flush).
//   * Memory is never freed individually; everything is released in ~Arena.
//   * Returned memory is uninitialized. Objects placed there are created with
//     placement new; their destructors are never run, so only put
//     trivially-destructible types there (enforce it: static_assert with
//     std::is_trivially_destructible_v at the call site).
//
// C++ focus: alignment arithmetic (alignof, std::align or bit tricks),
// std::unique_ptr<char[]> vs std::vector<std::unique_ptr<char[]>>,
// std::atomic<size_t> with the *weakest correct* memory order for the usage
// counter, and why this class is neither copyable nor movable.

#include <atomic>
#include <cstddef>

namespace strata {

class Arena {
 public:
  Arena();
  ~Arena();

  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;
  Arena(Arena&&) = delete;
  Arena& operator=(Arena&&) = delete;

  // Returns a pointer to `bytes` bytes of uninitialized memory. bytes > 0.
  [[nodiscard]] char* Allocate(std::size_t bytes);

  // Like Allocate, but aligned to alignof(std::max_align_t).
  [[nodiscard]] char* AllocateAligned(std::size_t bytes);

  // Total bytes reserved from the system, including per-block overhead.
  [[nodiscard]] std::size_t MemoryUsage() const;

 private:
  // TODO(M3): choose your representation. Consider: block size (4 KiB?), what
  // to do with requests bigger than a quarter block (give them their own
  // block, so a huge value doesn't waste the rest of the current block), and
  // how MemoryUsage() stays accurate without a lock.
};

}  // namespace strata
