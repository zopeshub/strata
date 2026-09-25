// Lab 08 (curriculum/08-wal-pattern.md): group commit, in miniature. A
// preview of M7 with no engine around it.
//
//   lab08_group_commit naive   8      each of 8 threads: lock, append, fsync, unlock
//   lab08_group_commit group   8      leader/follower: one fsync covers a whole group
//   lab08_group_commit sweep          both modes for 1, 2, 4, 8, 16, 32 threads
//
// Every thread performs 200 "commits" of a 128-byte record and may return
// only once ITS record is durable. Throughput = total commits / seconds.
// Predict the curve for both modes before you run the sweep.

#include <fcntl.h>
#include <unistd.h>

#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "lab_common.h"

namespace {

constexpr int kCommitsPerThread = 200;
constexpr std::size_t kRecord = 128;

class Log {
 public:
  explicit Log(const std::string& path)
      : fd_(::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644)) {
    lab::Check(fd_ >= 0, "open");
  }
  ~Log() { ::close(fd_); }
  Log(const Log&) = delete;
  Log& operator=(const Log&) = delete;

  // Given: the baseline. Correct, simple, slow.
  void CommitNaive(const std::string& rec) {
    std::lock_guard l(mu_);
    lab::Check(::write(fd_, rec.data(), rec.size()) == static_cast<ssize_t>(rec.size()), "write");
    lab::Check(::fsync(fd_) == 0, "fsync");
    syncs_++;
  }

  // YOURS: returns once `rec` is durable, sharing fsyncs between threads.
  // The classic shape (LevelDB's DBImpl::Write):
  //   * each caller enqueues a Writer{rec, done=false} and waits on a
  //     condition variable until it is done OR at the front of the queue;
  //   * the front writer is the LEADER: it gathers the records of everyone
  //     queued behind it (up to some byte limit), RELEASES the mutex,
  //     writes + fsyncs them all, re-acquires, marks them done, pops them,
  //     and wakes the next front writer (the next leader).
  // Why must the mutex be released during the fsync? Why must followers not
  // return until the leader has marked them done? What bounds the group size?
  void CommitGroup(const std::string& rec) {
    static_cast<void>(rec);
    LAB_TODO("CommitGroup: leader/follower group commit");
  }

  [[nodiscard]] long syncs() const { return syncs_; }

 private:
  int fd_;
  std::mutex mu_;
  std::condition_variable cv_;
  long syncs_ = 0;
  // TODO: your queue of waiting writers.
};

void Run(const std::string& mode, int threads) {
  Log log(lab::Dir() + "/lab08.log");
  const auto t0 = lab::Clock::now();
  std::vector<std::thread> ts;
  for (int t = 0; t < threads; t++) {
    ts.emplace_back([&, t] {
      const std::string rec(kRecord, static_cast<char>('a' + t % 26));
      for (int i = 0; i < kCommitsPerThread; i++) {
        if (mode == "naive") {
          log.CommitNaive(rec);
        } else {
          log.CommitGroup(rec);
        }
      }
    });
  }
  for (auto& t : ts) t.join();
  const double secs = lab::MicrosSince(t0) / 1e6;
  const long commits = static_cast<long>(threads) * kCommitsPerThread;
  std::printf("%-6s threads %3d: %8.0f commits/s, %6ld fsyncs (%.1f commits per fsync)\n",
              mode.c_str(), threads, static_cast<double>(commits) / secs, log.syncs(),
              static_cast<double>(commits) / static_cast<double>(std::max(log.syncs(), 1L)));
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "";
  if (mode == "sweep") {
    for (const char* m : {"naive", "group"}) {
      for (int t : {1, 2, 4, 8, 16, 32}) Run(m, t);
    }
    return 0;
  }
  if (mode != "naive" && mode != "group") {
    std::fprintf(stderr, "usage: %s naive|group <threads> | sweep\n", argv[0]);
    return 2;
  }
  Run(mode, argc > 2 ? std::stoi(argv[2]) : 8);
  return 0;
}
