#include "strata/todo.h"

#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>

namespace strata {

namespace {
std::string FormatTodo(std::string_view milestone, std::string_view what,
                       const std::source_location& loc) {
  return std::format("TODO({}) not implemented: {}\n    at {}:{} in {}", milestone, what,
                     loc.file_name(), loc.line(), loc.function_name());
}
}  // namespace

void TodoFailure(std::string_view milestone, std::string_view what, std::source_location loc) {
  throw NotImplementedError(FormatTodo(milestone, what, loc));
}

void TodoFailureNoexcept(std::string_view milestone, std::string_view what,
                         std::source_location loc) noexcept {
  std::fprintf(stderr, "%s\n", FormatTodo(milestone, what, loc).c_str());
  std::abort();
}

}  // namespace strata
