#pragma once

// K-way merge of sorted iterators. (M5)
//
// Yields the union of all children in comparator order. Entries that
// compare equal across children are ALL yielded (in unspecified relative
// order); deduplicating versions is the DB iterator's job, not this one.
// (For internal keys, "equal" never happens across a correct DB: sequence
// numbers are unique.)
//
// Implementation: a min-heap of child iterators (std::priority_queue with a
// custom comparator, or std::ranges::push_heap/pop_heap on a vector; which
// one lets you re-sift the top after Next() without pop+push?). With few
// children (< ~8), a linear scan for the smallest can beat the heap. Measure
// in bench/bench_merge.cc before believing either.
//
// status(): the first non-OK status of any child. A child that goes invalid
// with an error must stop the merge (don't silently skip its remaining keys).

#include <memory>
#include <vector>

#include "db/dbformat.h"
#include "strata/iterator.h"

namespace strata {

// `cmp` must outlive the returned iterator; it owns the children.
std::unique_ptr<Iterator> NewMergingIterator(const Comparator& cmp,
                                             std::vector<std::unique_ptr<Iterator>> children);

}  // namespace strata
