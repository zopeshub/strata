// Lab 06 (curriculum/06-io-uring.md): raw liburing, before you wrap it in
// coroutines in M8. LINUX ONLY (built only when liburing is found).
//
//   lab06_uring_read <file> <queue_depth>
//
// Reads the whole file in 4 KiB blocks with up to <queue_depth> reads in
// flight, verifies the bytes against pread, and prints throughput. Run
// with depth 1, 4, 16, 64 on a file bigger than RAM (or after dropping
// caches) and plot it. Where does it stop scaling, and why?

#include <fcntl.h>
#include <liburing.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "lab_common.h"

namespace {

constexpr std::size_t kBlock = 4096;

struct Slot {
  std::vector<char> buf = std::vector<char>(kBlock);
  off_t offset = 0;
};

// YOURS. Read every block of fd [0, file_size) keeping up to `depth` reads
// in flight. Use one Slot per in-flight read: io_uring_get_sqe,
// io_uring_prep_read(sqe, fd, slot.buf.data(), kBlock, slot.offset),
// io_uring_sqe_set_data(sqe, &slot), io_uring_submit; then
// io_uring_wait_cqe / io_uring_cqe_get_data / io_uring_cqe_seen, and reuse
// the slot for the next offset. For each completed block call
// verify(slot.offset, slot.buf.data(), cqe->res).
// Edge cases: cqe->res < 0 is -errno; a short read is legal (resubmit the rest).
// Return the number of bytes read.
template <class Verify>
std::size_t ReadAll(io_uring& ring, int fd, std::size_t file_size, unsigned depth,
                    Verify&& verify) {
  static_cast<void>(ring);
  static_cast<void>(fd);
  static_cast<void>(file_size);
  static_cast<void>(depth);
  static_cast<void>(verify);
  LAB_TODO("ReadAll with io_uring");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <file> <queue_depth>\n", argv[0]);
    return 2;
  }
  const int fd = ::open(argv[1], O_RDONLY);
  lab::Check(fd >= 0, "open");
  const auto size = static_cast<std::size_t>(::lseek(fd, 0, SEEK_END));
  const auto depth = static_cast<unsigned>(std::stoul(argv[2]));

  io_uring ring{};
  if (const int r = io_uring_queue_init(depth * 2, &ring, 0); r < 0) {
    std::fprintf(stderr, "io_uring_queue_init: %s\n", std::strerror(-r));
    return 1;
  }
  const int check_fd = ::open(argv[1], O_RDONLY);
  std::vector<char> expect(kBlock);
  std::size_t mismatches = 0;
  auto verify = [&](off_t off, const char* data, int n) {
    const ssize_t m = ::pread(check_fd, expect.data(), kBlock, off);
    if (n != m || std::memcmp(expect.data(), data, static_cast<std::size_t>(n)) != 0) mismatches++;
  };
  const auto t0 = lab::Clock::now();
  const std::size_t bytes = ReadAll(ring, fd, size, depth, verify);
  const double secs = lab::MicrosSince(t0) / 1e6;
  std::printf(
      "depth %3u: %zu MiB in %.2f s = %.1f MiB/s (includes verification preads), %zu mismatches\n",
      depth, bytes >> 20, secs, static_cast<double>(bytes) / 1048576.0 / secs, mismatches);
  io_uring_queue_exit(&ring);
  return mismatches == 0 && bytes == size ? 0 : 1;
}
