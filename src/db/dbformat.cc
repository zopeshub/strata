#include "db/dbformat.h"

#include "strata/todo.h"

namespace strata {

namespace {

class BytewiseComparatorImpl final : public Comparator {
 public:
  int Compare(std::string_view a, std::string_view b) const override { return a.compare(b); }
  std::string_view Name() const override { return "strata.BytewiseComparator"; }

  // A no-op is a correct implementation. TODO(M4-stretch): shorten `start`
  // to the shortest string in [start, limit) to shrink index blocks, e.g.
  // ("abcdefg", "abzz") -> "abd". Measure index-block size before/after.
  void FindShortestSeparator(std::string& /*start*/, std::string_view /*limit*/) const override {}
  void FindShortSuccessor(std::string& /*key*/) const override {}
};

}  // namespace

const Comparator* BytewiseComparator() {
  static const BytewiseComparatorImpl instance;
  return &instance;
}

std::uint64_t PackSequenceAndType(SequenceNumber /*seq*/, ValueType /*type*/) {
  STRATA_TODO("M2", "PackSequenceAndType");
}

void AppendInternalKey(std::string& /*dst*/, const ParsedInternalKey& /*key*/) {
  STRATA_TODO("M2", "AppendInternalKey");
}

std::string MakeInternalKey(std::string_view /*user_key*/, SequenceNumber /*seq*/,
                            ValueType /*type*/) {
  STRATA_TODO("M2", "MakeInternalKey");
}

std::optional<ParsedInternalKey> ParseInternalKey(std::string_view /*internal_key*/) {
  STRATA_TODO("M2", "ParseInternalKey");
}

std::string_view ExtractUserKey(std::string_view /*internal_key*/) {
  STRATA_TODO("M2", "ExtractUserKey");
}

int InternalKeyComparator::Compare(std::string_view /*a*/, std::string_view /*b*/) const {
  STRATA_TODO("M2", "InternalKeyComparator::Compare");
}

// TODO(M4-stretch): shorten the user-key part via user_->FindShortestSeparator
// and, if it got shorter, re-attach the trailer (kMaxSequenceNumber,
// kValueTypeForSeek) so the separator still sorts correctly. A no-op is correct.
void InternalKeyComparator::FindShortestSeparator(std::string& /*start*/,
                                                  std::string_view /*limit*/) const {}

void InternalKeyComparator::FindShortSuccessor(std::string& /*key*/) const {}

LookupKey::LookupKey(std::string_view /*user_key*/, SequenceNumber /*snapshot*/) {
  STRATA_TODO("M2", "LookupKey constructor");
}

std::string_view LookupKey::internal_key() const { STRATA_TODO("M2", "LookupKey::internal_key"); }

std::string_view LookupKey::user_key() const { STRATA_TODO("M2", "LookupKey::user_key"); }

}  // namespace strata
