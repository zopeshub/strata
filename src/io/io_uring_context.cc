// Linux-only io_uring backend (M8). Built only when liburing is found.
// Read curriculum/06-io-uring.md and do its lab first.

#include <liburing.h>

#include "io/io_context.h"
#include "strata/todo.h"

namespace strata {

namespace {

// TODO(M8):
//   * RAII over `struct io_uring` (io_uring_queue_init / io_uring_queue_exit).
//   * SubmitRead: io_uring_get_sqe (what if the SQ is full? submit and retry),
//     io_uring_prep_read, io_uring_sqe_set_data(sqe, &op). Count in-flight ops.
//   * RunUntilIdle: while in-flight > 0: io_uring_submit_and_wait(ring, 1),
//     then drain CQEs with io_uring_for_each_cqe / io_uring_cq_advance,
//     set op->result = cqe->res, and resume op->waiter. Careful: resuming a
//     coroutine may submit NEW reads (and even destroy the op you just
//     finished). Advance the CQ before or after resuming? Why?
//   * Stretch: registered buffers (io_uring_register_buffers) and
//     IORING_SETUP_SQPOLL; measure both in bench/bench_async_read.
class IoUringContext final : public IoContext {
 public:
  void SubmitRead(ReadOp& /*op*/) override { STRATA_TODO("M8", "IoUringContext::SubmitRead"); }
  bool RunUntilIdle() override { STRATA_TODO("M8", "IoUringContext::RunUntilIdle"); }
};

}  // namespace

Result<std::unique_ptr<IoContext>> NewIoUringContext(unsigned /*queue_depth*/) {
  STRATA_TODO("M8", "NewIoUringContext");
}

}  // namespace strata
