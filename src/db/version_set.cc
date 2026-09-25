#include "db/version_set.h"

#include "db/compaction.h"
#include "db/table_cache.h"
#include "strata/todo.h"

namespace strata {

Result<LookupResult> Version::Get(const ReadOptions& /*options*/, const LookupKey& /*key*/,
                                  TableCache& /*cache*/) const {
  STRATA_TODO("M5", "Version::Get (L0 newest-first, then binary search per level)");
}

void Version::AddIterators(const ReadOptions& /*options*/, TableCache& /*cache*/,
                           std::vector<std::unique_ptr<Iterator>>& /*out*/) const {
  STRATA_TODO("M5", "Version::AddIterators");
}

std::uint64_t Version::LevelBytes(int /*level*/) const { STRATA_TODO("M6", "Version::LevelBytes"); }

VersionSet::VersionSet(Path /*dir*/, const Options& /*options*/, FileSystem& /*fs*/,
                       TableCache& /*cache*/, const InternalKeyComparator& /*icmp*/) {
  STRATA_TODO("M6", "VersionSet constructor");
}

Status VersionSet::Recover() { STRATA_TODO("M6", "VersionSet::Recover"); }

Status VersionSet::LogAndApply(VersionEdit& /*edit*/, std::unique_lock<std::mutex>& /*lock*/) {
  STRATA_TODO("M6", "VersionSet::LogAndApply");
}

std::shared_ptr<const Version> VersionSet::current() const {
  STRATA_TODO("M6", "VersionSet::current");
}

std::uint64_t VersionSet::NewFileNumber() { STRATA_TODO("M6", "VersionSet::NewFileNumber"); }

SequenceNumber VersionSet::LastSequence() const { STRATA_TODO("M6", "VersionSet::LastSequence"); }

void VersionSet::SetLastSequence(SequenceNumber /*s*/) {
  STRATA_TODO("M6", "VersionSet::SetLastSequence");
}

std::uint64_t VersionSet::LogNumber() const { STRATA_TODO("M6", "VersionSet::LogNumber"); }

std::unique_ptr<Compaction> VersionSet::PickCompaction() {
  STRATA_TODO("M6", "VersionSet::PickCompaction");
}

void VersionSet::AddLiveFiles(std::vector<std::uint64_t>& /*live*/) const {
  STRATA_TODO("M6", "VersionSet::AddLiveFiles");
}

}  // namespace strata
