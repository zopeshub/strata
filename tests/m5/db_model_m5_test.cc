// Randomized model check for M5: memtables, flushes, L0 tables, iterators,
// snapshots, reopen. No compaction yet.

#include "support/db_model.h"

namespace strata::testing {
namespace {

class DbModelM5Test : public DbModelTest {
 protected:
  DbModelM5Test() {
    options_.write_buffer_size = 16 << 10;  // lots of flushes
    options_.disable_auto_compactions = true;
  }
};

TEST_P(DbModelM5Test, RandomOpsMatchModel) {
  const int n_seeds = GetParam() == FsKind::kMem ? 6 : 1;
  for (auto seed : Seeds(n_seeds)) {
    // Fresh DB per seed.
    ASSERT_OK(DB::Destroy(options_, dbpath_));
    RunModel(seed, {.ops = 2500, .key_space = 300, .compactions = false, .reopens = true});
    if (HasFatalFailure()) return;
  }
}

INSTANTIATE_TEST_SUITE_P(Fs, DbModelM5Test, AllFileSystems(), FsName);

}  // namespace
}  // namespace strata::testing
