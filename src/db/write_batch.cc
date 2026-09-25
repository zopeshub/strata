#include "strata/write_batch.h"

#include "strata/todo.h"
#include "util/coding.h"

namespace strata {

// TODO(M2): implement the wire format documented in write_batch.h.
//   - The 12-byte header (sequence, count) always exists, even when empty.
//   - Count() and Sequence() read the header; keep them O(1).
//   - ForEachImpl must validate as it decodes: unknown tag, truncated
//     varstring, or an op count that disagrees with the header -> Corruption.
//     This code runs on bytes read back from disk after a crash; treat them
//     as hostile input. (tests/m2/write_batch_test.cc feeds it garbage.)

WriteBatch::WriteBatch() { STRATA_TODO("M2", "WriteBatch constructor (12-byte header)"); }

void WriteBatch::Put(std::string_view /*key*/, std::string_view /*value*/) {
  STRATA_TODO("M2", "WriteBatch::Put");
}

void WriteBatch::Delete(std::string_view /*key*/) { STRATA_TODO("M2", "WriteBatch::Delete"); }

void WriteBatch::Clear() { STRATA_TODO("M2", "WriteBatch::Clear"); }

void WriteBatch::Append(const WriteBatch& /*other*/) { STRATA_TODO("M2", "WriteBatch::Append"); }

std::uint32_t WriteBatch::Count() const { STRATA_TODO("M2", "WriteBatch::Count"); }

std::size_t WriteBatch::ApproximateSize() const { return rep_.size(); }

std::uint64_t WriteBatch::Sequence() const { STRATA_TODO("M2", "WriteBatch::Sequence"); }

void WriteBatch::SetSequence(std::uint64_t /*seq*/) {
  STRATA_TODO("M2", "WriteBatch::SetSequence");
}

Result<WriteBatch> WriteBatch::FromContents(std::string_view /*contents*/) {
  STRATA_TODO("M2", "WriteBatch::FromContents");
}

Status WriteBatch::ForEachImpl(OpThunk /*thunk*/, void* /*ctx*/) const {
  STRATA_TODO("M2", "WriteBatch::ForEachImpl");
}

}  // namespace strata
