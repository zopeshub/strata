#pragma once

// WAL reader (M2). Recovery correctness lives here.
//
// Behavior spec (tests/m2/log_test.cc enforces all of it):
//   1. Returns logical records in order; nullopt at end of log.
//   2. Torn tail: if the file ends in the middle of a header or a payload,
//      or after some but not all fragments of a record (the writer crashed
//      mid-append), that is a clean end of log: return nullopt WITHOUT
//      reporting corruption. Crashing mid-write is normal, not corruption.
//   3. Checksum mismatch or a length field that runs past the end of the
//      block (when more data follows): report corruption and DROP THE REST OF
//      THE BLOCK, i.e. every unconsumed byte of the block buffer including
//      the bad header; that count is the `bytes` reported. Resume at the next
//      block boundary. Why the whole block? Once the length field is
//      untrusted, we cannot find the next header inside this block.
//      If this happens while assembling a fragmented record, the partial
//      record is dropped (and reported) too.
//   4. Fragment sequencing errors (MIDDLE/LAST without FIRST, FIRST/FULL
//      while assembling a fragmented record): report and drop the partial
//      record (bytes = its size so far, or the stray fragment's size); a
//      FIRST or FULL that interrupts restarts assembly with itself.
//      An unknown record type: report (bytes = fragment size) and skip it.
//   5. Zero-filled trailers (< kHeaderSize bytes at the end of a block) are
//      skipped silently. A header with type kZero and length 0 means
//      "preallocated, never written": skip the rest of the block silently.
//   6. An I/O error reading the file is reported and ends the log.
//
// Whether a reported corruption is fatal is the DB's decision
// (Options::paranoid_checks), not the reader's.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "db/log_format.h"
#include "strata/file_system.h"
#include "strata/status.h"

namespace strata::log {

class Reader {
 public:
  // Called for each dropped region: approximate bytes dropped and why.
  using CorruptionReporter = std::function<void(std::size_t bytes, const Status& reason)>;

  // `reporter` may be empty. If `verify_checksums` is false, CRCs are not
  // checked (used by strata_wal_dump --no-verify).
  Reader(std::unique_ptr<SequentialFile> file, CorruptionReporter reporter,
         bool verify_checksums = true);

  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;

  // Next logical record, or nullopt at end of log. The returned view points
  // into an internal buffer or into `scratch` and is valid until the next
  // call or until `scratch` is modified.
  std::optional<std::string_view> ReadRecord(std::string& scratch);

  // Physical offset of the start of the last record returned by ReadRecord.
  [[nodiscard]] std::uint64_t LastRecordOffset() const;

 private:
  // TODO(M2): file, reporter, checksum flag, a kBlockSize backing buffer,
  // the unconsumed view into it, eof flag, end-of-buffer offset, and a
  // helper like `ReadPhysicalRecord()` that returns either a fragment
  // (type + view) or a sentinel meaning "EOF" / "bad record".
  // C++: model that helper's result as a type, not a pair of magic ints.
};

}  // namespace strata::log
