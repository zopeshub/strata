// Lab 02 (curriculum/02-crash-consistency.md): is a new file durable
// after fsync(file)? Run under dm-flakey in the Lima VM via
// lab02_dmflakey.sh, which drops every write after this program exits
// (simulating power loss), then remounts and checks.
//
//   lab02_durable_create <dir> file-only     fsync the file, not the directory
//   lab02_durable_create <dir> with-dir      fsync the file AND the directory
//   lab02_durable_create <dir> check         after remount: does it exist, intact?
//
// Predict before running: on ext4, which mode loses the file? Would a
// different file system (xfs, btrfs) behave differently, and does it
// matter for code that must be correct on all of them?

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <string>

#include "lab_common.h"

namespace {

constexpr char kContents[] = "this line must survive power loss\n";

// YOURS: create <dir>/durable.txt with kContents and make the DATA durable
// (fsync the file). Nothing else.
void CreateAndSyncFile(const std::string& dir) {
  static_cast<void>(dir);
  LAB_TODO("CreateAndSyncFile: open O_CREAT, write all, fsync, close");
}

// YOURS: make the directory ENTRY durable.
void SyncDirectory(const std::string& dir) {
  static_cast<void>(dir);
  LAB_TODO("SyncDirectory: open the directory O_RDONLY (O_DIRECTORY on Linux) and fsync it");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <dir> file-only|with-dir|check\n", argv[0]);
    return 2;
  }
  const std::string dir = argv[1];
  const std::string mode = argv[2];
  if (mode == "check") {
    const int fd = ::open((dir + "/durable.txt").c_str(), O_RDONLY);
    if (fd < 0) {
      std::printf("durable.txt: MISSING after crash\n");
      return 1;
    }
    char buf[256] = {};
    const ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    const bool intact =
        n == static_cast<ssize_t>(sizeof(kContents) - 1) && std::string(buf) == kContents;
    std::printf("durable.txt: present, %zd bytes, %s\n", n, intact ? "intact" : "CONTENTS WRONG");
    return intact ? 0 : 1;
  }
  CreateAndSyncFile(dir);
  if (mode == "with-dir") SyncDirectory(dir);
  std::printf("created (%s)\n", mode.c_str());
  return 0;
}
