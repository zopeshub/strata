#pragma once

// Lock-free-read skiplist (M3). THE memory-model milestone.
//
// Concurrency contract:
//   * Writes (Insert) require external synchronization: one writer at a time.
//   * Reads (Contains, Iterator) need NO synchronization and may run
//     concurrently with one Insert.
//   * Nodes are never deleted while the list lives (they live in the Arena).
//
// How that can possibly be safe: a node is fully initialized (key, and its
// next pointers at every level) BEFORE it is published by storing its address
// into the predecessor's next pointer with RELEASE semantics. Readers load
// next pointers with ACQUIRE semantics, so if they see the node, they see its
// initialized contents. Levels are linked bottom-up so a reader never sees a
// node at level i that isn't yet reachable at level 0.
//
// Your job is to decide, for EVERY atomic access, the weakest memory order
// that is still correct, and to justify it in a comment. "seq_cst
// everywhere" passes the tests and fails the review.
//
// Things the reviewer will ask about:
//   * Node layout: `std::atomic<Node*> next_[1]` + over-allocation is the
//     classic LevelDB trick. Is it UB in C++? (Flexible array members are
//     not standard C++.) What would a standard-conforming alternative cost?
//     Is placement-new of an array of atomics into arena memory correct, and
//     do you need std::launder anywhere?
//   * max_height_: why can it be read with memory_order_relaxed, and what
//     happens if a reader sees a stale (smaller) value? A newer (larger) one?
//   * Random height: p = 1/4, kMaxHeight = 12. Why 1/4 and not 1/2?
//   * Why is `Prev()` O(log n) instead of O(1), and why is that the right
//     trade-off here?

#include <atomic>
#include <concepts>
#include <cstdint>

#include "strata/todo.h"
#include "util/arena.h"
#include "util/random.h"

namespace strata {

// A comparator returns <0, 0, >0 for (a, b).
template <class C, class K>
concept SkipListComparator =
    std::copy_constructible<C> && requires(const C& cmp, const K& a, const K& b) {
      { cmp(a, b) } -> std::convertible_to<int>;
    };

template <class Key, class Comparator>
  requires SkipListComparator<Comparator, Key>
class SkipList {
 private:
  struct Node;  // TODO(M3): define it (below, out of line, or right here).

 public:
  // `arena` must outlive the list. Nodes (and their keys) live in the arena.
  SkipList(Comparator cmp, Arena& arena) : compare_(cmp), arena_(&arena) {
    STRATA_TODO("M3", "SkipList constructor: allocate the head node");
  }

  SkipList(const SkipList&) = delete;
  SkipList& operator=(const SkipList&) = delete;

  // REQUIRES: nothing that compares equal to key is currently in the list.
  void Insert(const Key& /*key*/) { STRATA_TODO("M3", "SkipList::Insert"); }

  [[nodiscard]] bool Contains(const Key& /*key*/) const { STRATA_TODO("M3", "SkipList::Contains"); }

  // Iteration over the contents of a skip list. Safe to use concurrently with
  // one writer; it may or may not observe entries inserted after creation.
  class Iterator {
   public:
    explicit Iterator(const SkipList* list) : list_(list) {}

    [[nodiscard]] bool Valid() const { STRATA_TODO("M3", "SkipList::Iterator::Valid"); }
    // REQUIRES: Valid()
    [[nodiscard]] const Key& key() const { STRATA_TODO("M3", "SkipList::Iterator::key"); }
    // REQUIRES: Valid()
    void Next() { STRATA_TODO("M3", "SkipList::Iterator::Next"); }
    // REQUIRES: Valid()
    void Prev() { STRATA_TODO("M3", "SkipList::Iterator::Prev"); }
    // Advance to the first entry with key >= target.
    void Seek(const Key& /*target*/) { STRATA_TODO("M3", "SkipList::Iterator::Seek"); }
    void SeekToFirst() { STRATA_TODO("M3", "SkipList::Iterator::SeekToFirst"); }
    void SeekToLast() { STRATA_TODO("M3", "SkipList::Iterator::SeekToLast"); }

   private:
    [[maybe_unused]] const SkipList* list_;
    [[maybe_unused]] Node* node_ = nullptr;
  };

 private:
  static constexpr int kMaxHeight = 12;
  static constexpr unsigned kBranching = 4;

  // TODO(M3): head_, max_height_ (atomic), rnd_, and helpers such as
  // NewNode, RandomHeight, FindGreaterOrEqual(key, prev[]), FindLessThan,
  // FindLast, KeyIsAfterNode.

  [[maybe_unused]] Comparator const compare_;
  [[maybe_unused]] Arena* const arena_;
};

}  // namespace strata
