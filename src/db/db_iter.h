#pragma once

// The user-facing iterator (M5). Wraps a merged iterator over INTERNAL keys
// (memtable + immutable memtable + all SSTables) and presents USER keys:
//
//   * only entries with sequence <= `sequence` are visible (snapshot)
//   * for each user key, only the newest visible entry counts
//   * if that entry is a tombstone, the key is skipped entirely
//
// Example internal stream (user_key@seq:type), snapshot = 9:
//   a@12:V  a@7:V  a@3:D  b@8:D  b@2:V  c@5:V
//   -> yields a (from a@7), skips b (tombstone @8), yields c
//
// `keepalive` holds whatever the internal iterator borrows (memtables, the
// Version) so it can't be freed underneath the user. Think about which
// types fit in there without DBIter knowing about them (type erasure, again:
// std::shared_ptr<void> is one honest answer; is it a good one?).

#include <memory>

#include "db/dbformat.h"
#include "strata/iterator.h"

namespace strata {

std::unique_ptr<Iterator> NewDBIterator(std::unique_ptr<Iterator> internal_iter,
                                        SequenceNumber sequence, std::shared_ptr<void> keepalive);

}  // namespace strata
