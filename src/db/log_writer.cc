#include "db/log_writer.h"

#include "strata/todo.h"
#include "util/coding.h"
#include "util/crc32c.h"

namespace strata::log {

Writer::Writer(WritableFile& /*dest*/, std::uint64_t /*dest_length*/) {
  STRATA_TODO("M2", "log::Writer constructor");
}

// TODO(M2): the fragmentation loop. Pseudocode-level spec (write it yourself):
//   loop:
//     leftover = kBlockSize - block_offset
//     if leftover < kHeaderSize: zero-fill it (if > 0), block_offset = 0
//     avail = kBlockSize - block_offset - kHeaderSize
//     fragment = min(remaining payload, avail)
//     type = FULL / FIRST / MIDDLE / LAST depending on begin/end
//     emit header + fragment (ONE Append call per physical record, or two?
//       Think about what a crash between two Appends leaves on disk, and
//       whether the reader copes. Then decide.)
//   until the whole payload is written (note: runs at least once, so an
//   empty payload still produces a FULL record)
Status Writer::AddRecord(std::string_view /*payload*/) {
  STRATA_TODO("M2", "log::Writer::AddRecord");
}

}  // namespace strata::log
