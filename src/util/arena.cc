#include "util/arena.h"

#include "strata/todo.h"

namespace strata {

Arena::Arena() = default;

Arena::~Arena() = default;  // TODO(M3): release all blocks (or let RAII members do it).

char* Arena::Allocate(std::size_t /*bytes*/) { STRATA_TODO("M3", "Arena::Allocate"); }

char* Arena::AllocateAligned(std::size_t /*bytes*/) { STRATA_TODO("M3", "Arena::AllocateAligned"); }

std::size_t Arena::MemoryUsage() const { STRATA_TODO("M3", "Arena::MemoryUsage"); }

}  // namespace strata
