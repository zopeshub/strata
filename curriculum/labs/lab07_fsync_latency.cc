// Lab 07 (curriculum/07-fsync-semantics.md): what does "durable" cost?
//
//   lab07_fsync_latency            512 B append + sync, 500 times, per method
//
// Methods: none (write only), fsync, fdatasync, F_FULLFSYNC (macOS),
// F_BARRIERFSYNC (macOS). Fill in DoSync. Record the table for your machine
// in curriculum/NOTES.md (create it); you'll quote it in the M7 demo.
//
// Questions: on macOS, how many times slower is F_FULLFSYNC than fsync?
// What does that tell you about what plain fsync promises there? Inside
// the Lima VM, is fsync honest all the way to the Mac's SSD? (What
// would you need to know about the virtualization layer to be sure?)

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <vector>

#include "lab_common.h"

namespace {

enum class Method { kNone, kFsync, kFdatasync, kFullFsync, kBarrierFsync };

const char* Name(Method m) {
  switch (m) {
    case Method::kNone: return "write only";
    case Method::kFsync: return "write + fsync";
    case Method::kFdatasync: return "write + fdatasync";
    case Method::kFullFsync: return "write + F_FULLFSYNC";
    case Method::kBarrierFsync: return "write + F_BARRIERFSYNC";
  }
  return "?";
}

// YOURS. Return false if the method isn't available on this OS (so the
// row is skipped), true after a successful sync. Exit via lab::Check on
// errors. fdatasync exists on macOS: does it do anything different from
// fsync there? (man fdatasync on macOS.)
bool DoSync(int fd, Method m) {
  static_cast<void>(fd);
  if (m == Method::kNone) return true;
  LAB_TODO(
      "DoSync: fsync / fdatasync / fcntl(F_FULLFSYNC) / fcntl(F_BARRIERFSYNC), #ifdef __APPLE__");
}

}  // namespace

int main() {
  const std::string path = lab::Dir() + "/lab07.dat";
  const std::vector<char> rec(512, 'r');
  for (Method m : {Method::kNone, Method::kFsync, Method::kFdatasync, Method::kFullFsync,
                   Method::kBarrierFsync}) {
    const int fd = ::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    lab::Check(fd >= 0, "open");
    std::vector<double> us;
    bool supported = true;
    for (int i = 0; i < 500 && supported; i++) {
      const auto t0 = lab::Clock::now();
      lab::Check(::write(fd, rec.data(), rec.size()) == 512, "write");
      supported = DoSync(fd, m);
      us.push_back(lab::MicrosSince(t0));
    }
    if (supported) {
      lab::PrintLatencies(Name(m), us);
    } else {
      std::printf("%-28s (not available on this OS)\n", Name(m));
    }
    ::close(fd);
  }
  return 0;
}
