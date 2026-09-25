// Lab 03 (curriculum/03-virtual-memory.md): page faults, made visible.
//
// Maps a file and touches one byte per page in a given order, reporting
// minor and major faults (getrusage) and time per page.
//
//   lab03_mmap_faults create 1024          make a 1 GiB file (MiB argument)
//   lab03_mmap_faults seq                  touch pages in order
//   lab03_mmap_faults random               touch pages in random order
//   lab03_mmap_faults seq-advise           madvise(MADV_SEQUENTIAL) first
//   lab03_mmap_faults random-advise        madvise(MADV_RANDOM) first
//   lab03_mmap_faults anon                 anonymous memory, first touch
//
// Run each twice. Why is the second run all minor faults? Then evict the
// file from the page cache (lab04 has the commands) and compare major-fault
// counts for seq vs seq-advise. What is readahead doing?

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#include <cstdio>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "lab_common.h"

namespace {

struct Faults {
  long minor, major;
};

Faults Now() {
  rusage ru{};
  ::getrusage(RUSAGE_SELF, &ru);
  return {ru.ru_minflt, ru.ru_majflt};
}

// YOURS: touch (read) one byte in each page of [base, base + len), in the
// order given by `pages` (page indices). Return the sum of the bytes so the
// compiler cannot delete the loop. Why does that matter, and is `volatile`
// a better or worse way to do it?
std::uint64_t TouchPages(const char* base, std::size_t len, const std::vector<std::size_t>& pages,
                         long page_size) {
  static_cast<void>(base);
  static_cast<void>(len);
  static_cast<void>(pages);
  static_cast<void>(page_size);
  LAB_TODO("TouchPages");
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "";
  const std::string path = lab::Dir() + "/lab03.dat";
  const long page = ::sysconf(_SC_PAGESIZE);
  if (mode == "create") {
    const std::size_t mib = argc > 2 ? std::stoul(argv[2]) : 1024;
    const int fd = ::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    lab::Check(fd >= 0, "open");
    std::vector<char> chunk(1 << 20, 'x');
    for (std::size_t i = 0; i < mib; i++)
      lab::Check(::write(fd, chunk.data(), chunk.size()) > 0, "write");
    ::close(fd);
    std::printf("created %s (%zu MiB), page size %ld\n", path.c_str(), mib, page);
    return 0;
  }
  if (mode.empty()) {
    std::fprintf(stderr,
                 "usage: %s create [MiB] | seq | random | seq-advise | random-advise | anon\n",
                 argv[0]);
    return 2;
  }

  std::size_t len = 0;
  char* base = nullptr;
  if (mode == "anon") {
    len = std::size_t{1} << 30;
    void* p = ::mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    lab::Check(p != MAP_FAILED, "mmap anon");
    base = static_cast<char*>(p);
  } else {
    const int fd = ::open(path.c_str(), O_RDONLY);
    lab::Check(fd >= 0, "open (run 'create' first)");
    len = static_cast<std::size_t>(::lseek(fd, 0, SEEK_END));
    void* p = ::mmap(nullptr, len, PROT_READ, MAP_SHARED, fd, 0);
    lab::Check(p != MAP_FAILED, "mmap");
    ::close(fd);  // the mapping keeps the file referenced: why is this legal?
    base = static_cast<char*>(p);
  }
  if (mode.ends_with("-advise")) {
    const int advice = mode.starts_with("seq") ? MADV_SEQUENTIAL : MADV_RANDOM;
    lab::Check(::madvise(base, len, advice) == 0, "madvise");
  }

  std::vector<std::size_t> pages(len / static_cast<std::size_t>(page));
  std::iota(pages.begin(), pages.end(), 0);
  if (mode.starts_with("random")) std::shuffle(pages.begin(), pages.end(), std::mt19937_64(42));

  const Faults f0 = Now();
  const auto t0 = lab::Clock::now();
  const std::uint64_t sum = TouchPages(base, len, pages, page);
  const double us = lab::MicrosSince(t0);
  const Faults f1 = Now();
  std::printf(
      "%-14s pages %8zu  minor %8ld  major %8ld  %8.0f ms  %6.2f us/page  (checksum %llu)\n",
      mode.c_str(), pages.size(), f1.minor - f0.minor, f1.major - f0.major, us / 1000,
      us / static_cast<double>(pages.size()), static_cast<unsigned long long>(sum));
  ::munmap(base, len);
  return 0;
}
