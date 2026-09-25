#pragma once

#include <exception>
#include <thread>
#include <utility>
#include <vector>

namespace strata::testing {

// Runs `f` at scope exit, including when an ASSERT_* returns early or an
// exception unwinds. Use it to signal "I'm done" so other threads that spin
// on that signal can't hang when this one fails.
template <class F>
class OnExit {
 public:
  explicit OnExit(F f) : f_(std::move(f)) {}
  ~OnExit() { f_(); }
  OnExit(const OnExit&) = delete;
  OnExit& operator=(const OnExit&) = delete;

 private:
  F f_;
};

// Runs fn(0) .. fn(n-1) on n threads and joins them. An exception escaping a
// thread would std::terminate the whole test binary, so capture the first
// one and rethrow it on the calling thread (gtest reports it as a failure).
template <class Fn>
void RunConcurrently(int n, Fn fn) {
  std::vector<std::exception_ptr> errors(static_cast<std::size_t>(n));
  {
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; i++) {
      threads.emplace_back([&fn, &errors, i] {
        try {
          fn(i);
        } catch (...) {
          errors[static_cast<std::size_t>(i)] = std::current_exception();
        }
      });
    }
    for (auto& t : threads) t.join();
  }
  for (auto& e : errors) {
    if (e) std::rethrow_exception(e);
  }
}

}  // namespace strata::testing
