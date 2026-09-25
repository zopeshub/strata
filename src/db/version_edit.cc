#include "db/version_edit.h"

#include "strata/todo.h"
#include "util/coding.h"

namespace strata {

void VersionEdit::EncodeTo(std::string& /*dst*/) const {
  STRATA_TODO("M6", "VersionEdit::EncodeTo");
}

Result<VersionEdit> VersionEdit::DecodeFrom(std::string_view /*src*/) {
  STRATA_TODO("M6", "VersionEdit::DecodeFrom");
}

}  // namespace strata
