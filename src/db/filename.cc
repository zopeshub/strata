#include "db/filename.h"

#include "strata/todo.h"

namespace strata {

Path LogFileName(const Path& /*dir*/, std::uint64_t /*number*/) {
  STRATA_TODO("M2", "LogFileName (std::format with {:06})");
}

Path TableFileName(const Path& /*dir*/, std::uint64_t /*number*/) {
  STRATA_TODO("M2", "TableFileName");
}

Path ManifestFileName(const Path& /*dir*/, std::uint64_t /*number*/) {
  STRATA_TODO("M2", "ManifestFileName");
}

Path CurrentFileName(const Path& /*dir*/) { STRATA_TODO("M2", "CurrentFileName"); }

Path LockFileName(const Path& /*dir*/) { STRATA_TODO("M2", "LockFileName"); }

Path TempFileName(const Path& /*dir*/, std::uint64_t /*number*/) {
  STRATA_TODO("M2", "TempFileName");
}

std::optional<ParsedFileName> ParseFileName(std::string_view /*name*/) {
  STRATA_TODO("M2", "ParseFileName");
}

Status SetCurrentFile(FileSystem& /*fs*/, const Path& /*dir*/, std::uint64_t /*manifest_number*/) {
  STRATA_TODO("M6", "SetCurrentFile: write temp, sync temp, rename over CURRENT, sync dir");
}

}  // namespace strata
