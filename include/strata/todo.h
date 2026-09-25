#pragma once

// Scaffold-only. Every place where YOU write the logic calls STRATA_TODO.
// When a milestone is done, `tools/todo_report.sh M<n>` lists no call sites.
//
//   STRATA_TODO("M<n>", "what to implement here");
//
// Throws NotImplementedError, which gtest reports as a test failure with the
// milestone, file, and line, so a red test tells you exactly where to go.
//
// Inside functions that must not throw (destructors, noexcept moves) use
// STRATA_TODO_NOEXCEPT, which prints the same message and aborts.

#include <source_location>
#include <stdexcept>
#include <string_view>

namespace strata {

class NotImplementedError : public std::logic_error {
 public:
  using std::logic_error::logic_error;
};

[[noreturn]] void TodoFailure(std::string_view milestone, std::string_view what,
                              std::source_location loc);
[[noreturn]] void TodoFailureNoexcept(std::string_view milestone, std::string_view what,
                                      std::source_location loc) noexcept;

}  // namespace strata

#define STRATA_TODO(milestone, what) \
  ::strata::TodoFailure((milestone), (what), std::source_location::current())

#define STRATA_TODO_NOEXCEPT(milestone, what) \
  ::strata::TodoFailureNoexcept((milestone), (what), std::source_location::current())
