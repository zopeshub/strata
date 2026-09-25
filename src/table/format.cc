#include "table/format.h"

#include "strata/todo.h"
#include "util/coding.h"
#include "util/crc32c.h"

namespace strata {

void BlockHandle::EncodeTo(std::string& /*dst*/) const {
  STRATA_TODO("M4", "BlockHandle::EncodeTo");
}

std::optional<BlockHandle> BlockHandle::DecodeFrom(std::string_view& /*input*/) {
  STRATA_TODO("M4", "BlockHandle::DecodeFrom");
}

void Footer::EncodeTo(std::string& /*dst*/) const { STRATA_TODO("M4", "Footer::EncodeTo"); }

Result<Footer> Footer::DecodeFrom(std::string_view /*input*/) {
  STRATA_TODO("M4", "Footer::DecodeFrom");
}

// TODO(M4): one Read of handle.size + kBlockTrailerSize bytes. Watch out:
//   - a short read means the file is truncated -> Corruption, not IOError
//   - the RandomAccessFile may return a view that does NOT point into your
//     scratch buffer (mmap). Copy only if you have to.
//   - reject unknown compression types
Result<std::string> ReadBlock(const RandomAccessFile& /*file*/, const BlockHandle& /*handle*/,
                              bool /*verify_checksums*/) {
  STRATA_TODO("M4", "ReadBlock");
}

}  // namespace strata
