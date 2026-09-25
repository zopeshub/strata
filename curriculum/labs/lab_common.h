#pragma once

// Tiny helpers shared by the labs. Deliberately self-contained (no strata).

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <source_location>
#include <string>
#include <vector>

namespace lab {

// Where the labs write files. Override with LAB_DIR=/some/dir. In the Lima
// VM use a directory on the VM's disk (e.g. ~/lab), not the virtiofs mount.
inline std::string Dir() {
  const char* d = std::getenv("LAB_DIR");
  return d != nullptr ? d : "/tmp";
}

[[noreturn]] inline void Todo(const char* what,
                              std::source_location loc = std::source_location::current()) {
  std::fprintf(stderr, "LAB_TODO: %s\n    at %s:%u\n", what, loc.file_name(),
               static_cast<unsigned>(loc.line()));
  std::exit(3);
}

// Exits with a message if `ok` is false, showing errno.
inline void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "%s: %s\n", what, std::strerror(errno));
    std::exit(1);
  }
}

using Clock = std::chrono::steady_clock;

inline double MicrosSince(Clock::time_point t0) {
  return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

// Prints p50/p90/p99/p99.9/max of `us` (sorted in place).
inline void PrintLatencies(const char* label, std::vector<double>& us) {
  if (us.empty()) return;
  std::sort(us.begin(), us.end());
  auto p = [&](double q) {
    return us[static_cast<std::size_t>(q * static_cast<double>(us.size() - 1))];
  };
  std::printf("%-28s n=%-6zu p50 %9.1f us  p90 %9.1f  p99 %9.1f  p99.9 %9.1f  max %9.1f\n", label,
              us.size(), p(0.5), p(0.9), p(0.99), p(0.999), us.back());
}

}  // namespace lab

#define LAB_TODO(what) ::lab::Todo(what)
