// Lab 01 (curriculum/01-filesystems.md): atomic file replacement.
//
// A reader process reads a "config" file in a tight loop while a writer
// rewrites it thousands of times with a complete, self-checking payload
// ("<n>:<n repeated to 4 KiB>:<n>\n"). The reader reports every TORN read:
// empty, partial, or mixed old/new contents.
//
//   lab01_atomic_rename naive    # truncate + write in place: torn reads
//   lab01_atomic_rename atomic   # your version: must report 0 torn reads
//
// Then answer: your atomic version survives concurrent READERS. Does it
// also survive POWER LOSS at any instant? Which of your syscalls matter only
// for power loss, and not at all for this test? (You'll need them in M6,
// for CURRENT.)

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "lab_common.h"

namespace {

std::string Payload(int n) {
  std::string body = std::to_string(n) + ":";
  while (body.size() < 4096) body += std::to_string(n);
  return body + ":" + std::to_string(n) + "\n";
}

bool IsWhole(const std::string& s) {
  if (s.empty() || s.back() != '\n') return false;
  const auto first = s.find(':');
  const auto last = s.rfind(':');
  if (first == std::string::npos || first == last) return false;
  return s.substr(0, first) == s.substr(last + 1, s.size() - last - 2) &&
         s == Payload(std::stoi(s.substr(0, first)));
}

void WriteAll(int fd, const std::string& data) {
  std::size_t off = 0;
  while (off < data.size()) {
    const ssize_t n = ::write(fd, data.data() + off, data.size() - off);
    lab::Check(n >= 0, "write");
    off += static_cast<std::size_t>(n);
  }
}

// Given: the obvious, wrong way.
void NaiveReplace(const std::string& path, const std::string& contents) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  lab::Check(fd >= 0, "open");
  WriteAll(fd, contents);
  ::close(fd);
}

// YOURS. Replace `path` so that every reader, at every instant, sees either
// the complete old contents or the complete new contents. Also make it
// durable (the new contents survive power loss once this returns), even
// though this test can't observe that part. Hints in 01-filesystems.md.
void AtomicReplace(const std::string& path, const std::string& contents) {
  static_cast<void>(path);
  static_cast<void>(contents);
  LAB_TODO("AtomicReplace: temp file, write, fsync, rename, fsync the directory");
}

std::string ReadFile(const std::string& path) {
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) return "<ENOENT>";
  std::string out;
  char buf[8192];
  ssize_t n;
  while ((n = ::read(fd, buf, sizeof(buf))) > 0) out.append(buf, static_cast<std::size_t>(n));
  ::close(fd);
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "";
  if (mode != "naive" && mode != "atomic") {
    std::fprintf(stderr, "usage: %s naive|atomic\n", argv[0]);
    return 2;
  }
  const std::string path = lab::Dir() + "/lab01_config";
  NaiveReplace(path, Payload(0));

  int pipefd[2];
  lab::Check(::pipe(pipefd) == 0, "pipe");
  const pid_t reader = ::fork();
  lab::Check(reader >= 0, "fork");
  if (reader == 0) {
    ::close(pipefd[1]);
    ::fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    long reads = 0, torn = 0, missing = 0;
    char c;
    while (::read(pipefd[0], &c, 1) != 0) {  // until the writer closes the pipe
      const std::string s = ReadFile(path);
      reads++;
      if (s == "<ENOENT>") {
        missing++;
      } else if (!IsWhole(s)) {
        if (torn++ < 3)
          std::printf("  torn read #%ld: %zu bytes, starts '%.12s'\n", torn, s.size(), s.c_str());
      }
    }
    std::printf("reader: %ld reads, %ld torn, %ld missing-file\n", reads, torn, missing);
    std::fflush(stdout);
    std::_Exit(torn + missing == 0 ? 0 : 1);
  }
  ::close(pipefd[0]);
  const auto t0 = lab::Clock::now();
  constexpr int kWrites = 3000;
  for (int i = 1; i <= kWrites; i++) {
    if (mode == "naive") {
      NaiveReplace(path, Payload(i));
    } else {
      AtomicReplace(path, Payload(i));
    }
  }
  std::printf("writer: %d replacements in %.0f ms (%s)\n", kWrites, lab::MicrosSince(t0) / 1000,
              mode.c_str());
  ::close(pipefd[1]);
  int status = 0;
  ::waitpid(reader, &status, 0);
  const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
  std::printf("%s\n", ok ? "no torn reads" : "TORN READS OBSERVED");
  return ok ? 0 : 1;
}
