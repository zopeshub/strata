#pragma once

// The in-memory write buffer. (M2: simple version, M3: concurrent version)
//
// M2: back it with an ordered std::map (or std::set) keyed by internal key,
//     ordered by InternalKeyComparator. The DB serializes all access with a
//     mutex. Goal: correctness and a clean interface, fast.
//     C++: a *transparent* comparator (is_transparent) so lookups by
//     std::string_view don't allocate a std::string per Get.
//
// M3: replace the internals with Arena + SkipList (src/db/skiplist.h). The
//     interface does NOT change, and every M2 memtable test must still pass.
//     New guarantee: Get() and iterators may run concurrently with one
//     writer calling Add(), without any lock.
//
// Entries are immutable once added; there is no in-place update. An
// overwrite is a new entry with a higher sequence number; a delete is a
// tombstone entry (ValueType::kDeletion, empty value).

#include <cstddef>
#include <memory>
#include <string_view>

#include "db/dbformat.h"
#include "strata/iterator.h"

namespace strata {

class MemTable {
 public:
  explicit MemTable(const InternalKeyComparator& comparator);
  ~MemTable();

  MemTable(const MemTable&) = delete;
  MemTable& operator=(const MemTable&) = delete;

  // REQUIRES: no entry with the same (user_key, seq) exists. Externally
  // synchronized with other Add() calls.
  void Add(SequenceNumber seq, ValueType type, std::string_view user_key, std::string_view value);

  // Newest entry for key.user_key() with sequence <= the lookup's sequence.
  [[nodiscard]] LookupResult Get(const LookupKey& key) const;

  // Iterates entries in InternalKeyComparator order; key() is the internal
  // key. The iterator must not outlive this memtable (callers keep a
  // shared_ptr<MemTable> alive for as long as the iterator exists).
  [[nodiscard]] std::unique_ptr<Iterator> NewIterator() const;

  // Bytes of memory attributable to this memtable. Safe to call from any
  // thread concurrently with Add() (after M3).
  [[nodiscard]] std::size_t ApproximateMemoryUsage() const;

 private:
  // TODO(M2): representation (ordered container + comparator adapter).
  // TODO(M3): Arena + SkipList<const char*, KeyComparator>, where each entry
  //           is one arena allocation:
  //             varint32(internal_key_len) internal_key varint32(value_len) value
  //           Why length-prefix inside the arena instead of storing
  //           string_views? (Think: size of a skiplist node, cache lines.)
};

}  // namespace strata
