#include "db/log_reader.h"

#include "strata/todo.h"
#include "util/coding.h"
#include "util/crc32c.h"

namespace strata::log {

Reader::Reader(std::unique_ptr<SequentialFile> /*file*/, CorruptionReporter /*reporter*/,
               bool /*verify_checksums*/) {
  STRATA_TODO("M2", "log::Reader constructor");
}

std::optional<std::string_view> Reader::ReadRecord(std::string& /*scratch*/) {
  STRATA_TODO("M2", "log::Reader::ReadRecord");
}

std::uint64_t Reader::LastRecordOffset() const {
  STRATA_TODO("M2", "log::Reader::LastRecordOffset");
}

}  // namespace strata::log
