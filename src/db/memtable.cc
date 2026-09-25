#include "db/memtable.h"

#include "strata/todo.h"

namespace strata {

MemTable::MemTable(const InternalKeyComparator& /*comparator*/) {
  STRATA_TODO("M2", "MemTable constructor");
}

MemTable::~MemTable() = default;

void MemTable::Add(SequenceNumber /*seq*/, ValueType /*type*/, std::string_view /*user_key*/,
                   std::string_view /*value*/) {
  STRATA_TODO("M2", "MemTable::Add");
}

LookupResult MemTable::Get(const LookupKey& /*key*/) const { STRATA_TODO("M2", "MemTable::Get"); }

// TODO(M2): define a concrete Iterator subclass (in an anonymous namespace)
// that walks your container. With std::map, what invalidates it? (Nothing
// here: map iterators survive inserts. Will that still be true in M3?)
std::unique_ptr<Iterator> MemTable::NewIterator() const {
  STRATA_TODO("M2", "MemTable::NewIterator");
}

std::size_t MemTable::ApproximateMemoryUsage() const {
  STRATA_TODO("M2", "MemTable::ApproximateMemoryUsage");
}

}  // namespace strata
