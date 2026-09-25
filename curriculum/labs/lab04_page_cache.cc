// Lab 04 (curriculum/04-page-cache.md): cold vs warm reads, and asking the
// kernel what is cached.
//
//   lab04_page_cache create 512     make a 512 MiB file
//   lab04_page_cache resident       what fraction of the file is in the page cache? (mincore)
//   lab04_page_cache evict          drop THIS file's pages from the cache (your code)
//   lab04_page_cache read           4 KiB random preads: latency percentiles
//   lab04_page_cache read-direct    same, bypassing the cache (O_DIRECT / F_NOCACHE)
//
// Sequence to run: create, resident, read, resident, evict, resident, read,
// read-direct. Explain every number. Whole-system alternatives to `evict`:
//   macOS: sudo purge
//   Linux: sync; echo 1 | sudo tee /proc/sys/vm/drop_caches
// Why is dropping only one file's pages the better experiment?

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "lab_common.h"

namespace {

const std::string& Path() {
  static const std::string p = lab::Dir() + "/lab04.dat";
  return p;
}

// YOURS: fraction of the file's pages resident in the page cache.
// mmap the file (PROT_READ, MAP_SHARED), call mincore() on the whole
// mapping, count entries with the low bit set. Note mincore's vector
// element type differs: `char*` on macOS, `unsigned char*` on Linux.
// Does mmap + mincore itself pull pages in? (Test it.)
double ResidentFraction(int fd, std::size_t len) {
  static_cast<void>(fd);
  static_cast<void>(len);
  LAB_TODO("ResidentFraction via mmap + mincore");
}

// YOURS: evict this file's pages.
//   Linux: posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED). Dirty pages are
//          not dropped, so fdatasync first.
//   macOS: no fadvise. One trick: msync(MS_INVALIDATE) on an mmap of the
//          file. Measure with `resident` whether it works; if not, fall back
//          to `sudo purge` and explain why the API is missing.
void Evict(int fd, std::size_t len) {
  static_cast<void>(fd);
  static_cast<void>(len);
  LAB_TODO("Evict one file's pages");
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "";
  if (mode == "create") {
    const std::size_t mib = argc > 2 ? std::stoul(argv[2]) : 512;
    const int fd = ::open(Path().c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    lab::Check(fd >= 0, "open");
    std::vector<char> chunk(1 << 20);
    std::mt19937_64 rng(1);
    for (auto& c : chunk) c = static_cast<char>(rng());
    for (std::size_t i = 0; i < mib; i++)
      lab::Check(::write(fd, chunk.data(), chunk.size()) > 0, "write");
    ::fsync(fd);
    ::close(fd);
    std::printf("created %s, %zu MiB (it is probably fully cached right now: check)\n",
                Path().c_str(), mib);
    return 0;
  }
  const bool direct = mode == "read-direct";
  int flags = O_RDONLY;
#if defined(__linux__)
  if (direct) flags |= O_DIRECT;
#endif
  const int fd = ::open(Path().c_str(), flags);
  lab::Check(fd >= 0, "open (run 'create' first)");
#if defined(__APPLE__)
  if (direct) lab::Check(::fcntl(fd, F_NOCACHE, 1) == 0, "F_NOCACHE");
#endif
  const auto len = static_cast<std::size_t>(::lseek(fd, 0, SEEK_END));

  if (mode == "resident") {
    std::printf("resident: %.1f%% of %zu MiB\n", 100.0 * ResidentFraction(fd, len), len >> 20);
  } else if (mode == "evict") {
    Evict(fd, len);
    std::printf("evicted; now resident: %.1f%%\n", 100.0 * ResidentFraction(fd, len));
  } else if (mode == "read" || direct) {
    constexpr std::size_t kBlock = 4096;
    void* raw = std::aligned_alloc(kBlock, kBlock);  // O_DIRECT needs alignment
    std::mt19937_64 rng(7);
    std::vector<double> us;
    for (int i = 0; i < 5000; i++) {
      const auto off = static_cast<off_t>((rng() % (len / kBlock)) * kBlock);
      const auto t0 = lab::Clock::now();
      lab::Check(::pread(fd, raw, kBlock, off) == static_cast<ssize_t>(kBlock), "pread");
      us.push_back(lab::MicrosSince(t0));
    }
    std::free(raw);
    lab::PrintLatencies(direct ? "4K random pread (direct)" : "4K random pread", us);
  } else {
    std::fprintf(stderr, "usage: %s create [MiB] | resident | evict | read | read-direct\n",
                 argv[0]);
    return 2;
  }
  ::close(fd);
  return 0;
}
