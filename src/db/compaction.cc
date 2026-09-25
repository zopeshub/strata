#include "db/compaction.h"

#include "strata/todo.h"

namespace strata {

int Compaction::level() const { STRATA_TODO("M6", "Compaction::level"); }

const std::vector<std::shared_ptr<const FileMetaData>>& Compaction::inputs(int /*which*/) const {
  STRATA_TODO("M6", "Compaction::inputs");
}

bool Compaction::IsTrivialMove() const { STRATA_TODO("M6", "Compaction::IsTrivialMove"); }

bool Compaction::IsBaseLevelForKey(std::string_view /*user_key*/) {
  STRATA_TODO("M6", "Compaction::IsBaseLevelForKey");
}

bool Compaction::ShouldStopBefore(std::string_view /*internal_key*/) {
  STRATA_TODO("M6", "Compaction::ShouldStopBefore");
}

void Compaction::AddInputDeletions(VersionEdit& /*edit*/) const {
  STRATA_TODO("M6", "Compaction::AddInputDeletions");
}

}  // namespace strata
