// Randomized model check for M6: everything in M5 plus background and
// manual compactions, with tiny buffers so the tree gets several levels deep.

#include "support/db_model.h"

namespace strata::testing {
namespace {

class DbModelM6Test : public DbModelTest {
 protected:
  DbModelM6Test() {
    options_.write_buffer_size = 8 << 10;
    options_.target_file_size = 8 << 10;
    options_.max_bytes_for_level_base = 32 << 10;
    options_.max_bytes_for_level_multiplier = 3;
    options_.l0_compaction_trigger = 2;
    options_.l0_slowdown_writes_trigger = 4;
    options_.l0_stop_writes_trigger = 6;
  }
};

TEST_P(DbModelM6Test, RandomOpsMatchModel) {
  const int n_seeds = GetParam() == FsKind::kMem ? 8 : 2;
  for (auto seed : Seeds(n_seeds)) {
    ASSERT_OK(DB::Destroy(options_, dbpath_));
    RunModel(seed, {.ops = 4000, .key_space = 500, .compactions = true, .reopens = true});
    if (HasFatalFailure()) return;
  }
}

TEST_P(DbModelM6Test, HotKeySpace) {
  for (auto seed : Seeds(3)) {
    ASSERT_OK(DB::Destroy(options_, dbpath_));
    RunModel(seed + 1, {.ops = 4000, .key_space = 20, .compactions = true, .reopens = true});
    if (HasFatalFailure()) return;
  }
}

INSTANTIATE_TEST_SUITE_P(Fs, DbModelM6Test, AllFileSystems(), FsName);

}  // namespace
}  // namespace strata::testing
