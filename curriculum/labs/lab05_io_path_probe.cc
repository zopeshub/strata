// Lab 05 (curriculum/05-io-paths.md): follow one write from write(2) to
// the device. This program is complete; the lab is TRACING it.
//
//   lab05_io_path_probe buffered   write 64 x 4 KiB, then one fsync
//   lab05_io_path_probe sync       write 64 x 4 KiB, fsync after each
//   lab05_io_path_probe odsync     open(O_DSYNC), write 64 x 4 KiB
//
// Linux (Lima VM, on the VM disk, not virtiofs):
//   strace -f -T -e trace=openat,write,fsync,fdatasync ./lab05_io_path_probe sync
//   sudo bpftrace curriculum/labs/lab05_block_latency.bt &  then run the probe
//   sudo perf trace -s ./lab05_io_path_probe buffered
// macOS:
//   sudo fs_usage -w -f filesys lab05_io_path_probe   (in another terminal, then run it)
//
// Questions: in `buffered` mode, when do block I/Os actually get issued:
// during the writes, at fsync, or later? How many block I/Os does one
// fsync cause in `sync` mode, and why more than one (journal!)? What does
// O_DSYNC change compared to write + fdatasync?

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <vector>

#include "lab_common.h"

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "buffered";
  const std::string path = lab::Dir() + "/lab05.dat";
  int flags = O_CREAT | O_WRONLY | O_TRUNC;
  if (mode == "odsync") flags |= O_DSYNC;
  const int fd = ::open(path.c_str(), flags, 0644);
  lab::Check(fd >= 0, "open");
  const std::vector<char> block(4096, 'z');
  std::vector<double> us;
  const auto total0 = lab::Clock::now();
  for (int i = 0; i < 64; i++) {
    const auto t0 = lab::Clock::now();
    lab::Check(::write(fd, block.data(), block.size()) == 4096, "write");
    if (mode == "sync") lab::Check(::fsync(fd) == 0, "fsync");
    us.push_back(lab::MicrosSince(t0));
  }
  if (mode == "buffered") {
    const auto t0 = lab::Clock::now();
    lab::Check(::fsync(fd) == 0, "fsync");
    std::printf("final fsync: %.1f us\n", lab::MicrosSince(t0));
  }
  lab::PrintLatencies(("per-write (" + mode + ")").c_str(), us);
  std::printf("total: %.1f ms\n", lab::MicrosSince(total0) / 1000);
  ::close(fd);
  return 0;
}
