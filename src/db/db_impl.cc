#include "db/db_impl.h"

#include "db/dbformat.h"
#include "db/filename.h"
#include "db/log_reader.h"
#include "db/log_writer.h"
#include "db/memtable.h"
#include "strata/todo.h"

namespace strata {

Result<std::unique_ptr<DB>> DB::Open(const Options& /*options*/,
                                     const std::filesystem::path& /*dir*/) {
  // TODO(M2): validate options, construct DBImpl, Recover(), return it.
  // Semantics: create_if_missing / error_if_exists (InvalidArgument), a
  // missing dir without create_if_missing (InvalidArgument), a DB already
  // open by anyone (Busy, from the LOCK file).
  STRATA_TODO("M2", "DB::Open");
}

Status DB::Destroy(const Options& /*options*/, const std::filesystem::path& /*dir*/) {
  // TODO(M2): take the lock (fail if the DB is open), delete every file whose
  // name ParseFileName() recognizes, release the lock, delete LOCK, remove
  // the dir. Leave unrecognized files alone (and then the dir can't be
  // removed: is that an error?).
  STRATA_TODO("M2", "DB::Destroy");
}

DBImpl::DBImpl(const Options& /*options*/, std::filesystem::path /*dir*/) {
  STRATA_TODO("M2", "DBImpl constructor");
}

DBImpl::~DBImpl() = default;  // TODO(M5): stop background work, then release resources.

Status DBImpl::Recover() { STRATA_TODO("M2", "DBImpl::Recover"); }

Status DBImpl::Put(const WriteOptions& /*options*/, std::string_view /*key*/,
                   std::string_view /*value*/) {
  STRATA_TODO("M2", "DBImpl::Put");
}

Status DBImpl::Delete(const WriteOptions& /*options*/, std::string_view /*key*/) {
  STRATA_TODO("M2", "DBImpl::Delete");
}

Status DBImpl::Write(const WriteOptions& /*options*/, WriteBatch& /*batch*/) {
  STRATA_TODO("M2", "DBImpl::Write");
}

Result<std::optional<std::string>> DBImpl::Get(const ReadOptions& /*options*/,
                                               std::string_view /*key*/) {
  STRATA_TODO("M2", "DBImpl::Get");
}

std::unique_ptr<Iterator> DBImpl::NewIterator(const ReadOptions& /*options*/) {
  STRATA_TODO("M5", "DBImpl::NewIterator");
}

std::shared_ptr<const Snapshot> DBImpl::GetSnapshot() { STRATA_TODO("M5", "DBImpl::GetSnapshot"); }

std::optional<std::string> DBImpl::GetProperty(std::string_view /*name*/) {
  STRATA_TODO("M5", "DBImpl::GetProperty");
}

Status DBImpl::Flush() { STRATA_TODO("M5", "DBImpl::Flush"); }

Status DBImpl::CompactRange(std::optional<std::string_view> /*begin*/,
                            std::optional<std::string_view> /*end*/) {
  STRATA_TODO("M6", "DBImpl::CompactRange");
}

Status DBImpl::WaitForBackgroundWork() { STRATA_TODO("M5", "DBImpl::WaitForBackgroundWork"); }

}  // namespace strata
