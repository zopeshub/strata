#include "io/io_context.h"

#include "strata/todo.h"

namespace strata {

namespace {

// TODO(M8): pread() in SubmitRead. Should it resume the coroutine right
// there (inside await_suspend's call stack) or queue it and resume in
// RunUntilIdle()? One of these recurses unboundedly for a loop of reads.
class BlockingIoContext final : public IoContext {
 public:
  void SubmitRead(ReadOp& /*op*/) override { STRATA_TODO("M8", "BlockingIoContext::SubmitRead"); }
  bool RunUntilIdle() override { STRATA_TODO("M8", "BlockingIoContext::RunUntilIdle"); }
};

}  // namespace

std::unique_ptr<IoContext> NewBlockingIoContext() { return std::make_unique<BlockingIoContext>(); }

}  // namespace strata
