#pragma once

// Task<T>: a lazy, single-consumer coroutine return type. (M8)
//
//   Task<int> ReadAndParse(IoContext& io, int fd) {
//     char buf[4096];
//     auto n = co_await io.Read(fd, 0, buf);
//     co_return Parse(buf, *n);
//   }
//
// Semantics to implement (tests/m8/task_test.cc checks each one):
//   * LAZY: calling a coroutine returning Task does not run its body; the
//     body starts when the Task is first co_awaited (or run by SyncWait).
//   * co_await task: suspends the awaiter, starts/resumes the task, and when
//     the task finishes, resumes the awaiter WITH SYMMETRIC TRANSFER
//     (await_suspend returns the continuation's handle). Without it, a loop
//     that co_awaits a million synchronously-completing tasks overflows the
//     stack; the test does exactly that.
//   * co_return v: the value is moved out by the awaiter (Task<T> works for
//     move-only T like std::unique_ptr).
//   * Exceptions thrown in the body are captured (std::exception_ptr) and
//     rethrown from co_await. (strata code returns Status, but Task must be
//     correct for any body.)
//   * The Task owns the coroutine frame: destroying the Task destroys the
//     frame (RAII over std::coroutine_handle). Task is move-only.
//
// C++ focus: promise_type, initial/final suspend, awaiter protocol
// (await_ready / await_suspend / await_resume), coroutine_handle lifetime,
// std::exception_ptr, and "who destroys the frame, and when?".
//
// The skeleton below shows the REQUIRED SHAPE so everything compiles; every
// body is yours. Add members freely.

#include <coroutine>
#include <exception>
#include <utility>
#include <vector>

#include "strata/todo.h"

namespace strata {

template <class T>
class Task;

namespace detail {

template <class T>
struct TaskPromise {
  Task<T> get_return_object() noexcept;
  std::suspend_always initial_suspend() noexcept { return {}; }  // lazy: this one is given
  auto final_suspend() noexcept {
    // TODO(M8): return an awaiter whose await_suspend(handle) returns the
    // continuation (symmetric transfer) or std::noop_coroutine().
    return std::suspend_always{};
  }
  template <class U>
  void return_value(U&& /*value*/) {
    STRATA_TODO("M8", "TaskPromise::return_value");
  }
  void unhandled_exception() noexcept {
    STRATA_TODO_NOEXCEPT("M8", "TaskPromise::unhandled_exception (store std::current_exception())");
  }
  // TODO(M8): storage for the result (think: std::variant<std::monostate, T,
  // std::exception_ptr>) and the continuation handle.
};

}  // namespace detail

template <class T>
class [[nodiscard]] Task {
 public:
  using promise_type = detail::TaskPromise<T>;
  using handle_type = std::coroutine_handle<promise_type>;

  Task() noexcept = default;
  explicit Task(handle_type h) noexcept : handle_(h) {}
  ~Task() {
    // TODO(M8): destroy the frame if we own one.
  }

  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  Task(Task&& /*other*/) noexcept { STRATA_TODO_NOEXCEPT("M8", "Task move constructor"); }
  Task& operator=(Task&& /*other*/) noexcept { STRATA_TODO_NOEXCEPT("M8", "Task move assignment"); }

  // The awaiter returned by `co_await task`.
  auto operator co_await() && noexcept {
    struct Awaiter {
      handle_type handle;
      bool await_ready() noexcept { STRATA_TODO_NOEXCEPT("M8", "Task awaiter await_ready"); }
      std::coroutine_handle<> await_suspend(std::coroutine_handle<> /*awaiting*/) noexcept {
        STRATA_TODO_NOEXCEPT("M8",
                             "Task awaiter await_suspend (store continuation, return handle)");
      }
      T await_resume() {
        STRATA_TODO("M8", "Task awaiter await_resume (rethrow or move value out)");
      }
    };
    return Awaiter{handle_};
  }

  // For SyncWait / event loops.
  [[nodiscard]] handle_type handle() const noexcept { return handle_; }

 private:
  handle_type handle_{};
};

template <class T>
Task<T> detail::TaskPromise<T>::get_return_object() noexcept {
  return Task<T>{Task<T>::handle_type::from_promise(*this)};
}

// Runs `task` to completion on the calling thread with no I/O event loop:
// only valid for tasks that never suspend on real I/O. (For I/O, use
// IoContext::RunUntilComplete.) Rethrows the task's exception.
template <class T>
T SyncWait(Task<T> /*task*/) {
  // TODO(M8): the classic trick is a tiny wrapper coroutine type whose
  // final_suspend signals completion; for a single-threaded driver, just
  // resume the handle and check done().
  STRATA_TODO("M8", "SyncWait");
}

// Starts every task, and completes when ALL have completed, with their
// results in input order. This is what gets many reads in flight at once:
//
//   std::vector<Task<Result<std::size_t>>> reads = ...;
//   auto results = co_await WhenAll(std::move(reads));
//
// Design questions: how do N children resume ONE parent exactly once (and
// only after the last child finishes)? A shared countdown in the parent's
// frame, decremented by each child's final_suspend? Which child resumes the
// parent? What if every child completes synchronously, before the parent
// has even suspended? (That race exists even single-threaded.) If child 3
// throws, what do you do with children 4..N? Record your answers in an ADR.
//
// You may change the signature (e.g. a variadic version returning a tuple)
// as long as tests/m8 still express the same thing.
template <class T>
Task<std::vector<T>> WhenAll(std::vector<Task<T>> /*tasks*/) {
  STRATA_TODO("M8", "WhenAll");
}

// TODO(M8-stretch, C++): Task<void>. A promise can't have both return_value
// and return_void, so this needs a partial specialization of TaskPromise
// (or a CRTP base shared by both). Tests only use non-void T.

}  // namespace strata
