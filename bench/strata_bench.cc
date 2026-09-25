// strata_bench: end-to-end workloads against the public DB API, in the
// spirit of LevelDB's db_bench.
//
//   ./build/release/bench/strata_bench --benchmarks=fillseq,readrandom --num=1000000
//   ./build/release/bench/strata_bench --benchmarks=fillsync --num=2000 --threads=8
//
// Workloads:
//   fillseq      write --num keys in order (async WAL)
//   fillrandom   write --num keys in random order (async WAL)
//   overwrite    random writes over the existing key space
//   fillsync     random writes with sync=true (fsync per write, or per group)
//   readrandom   random point reads of existing keys
//   readmissing  random point reads of keys that don't exist (bloom filters!)
//   readseq      full scan with an iterator
//   compact      CompactRange over everything
//   stats        print strata.stats and write amplification
//
// Unless --use_existing_db=1, the DB at --db is destroyed first.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "histogram.h"
#include "strata/db.h"
#include "strata/file_system.h"
#include "strata/options.h"
#include "util/random.h"

namespace strata::bench {
namespace {

struct Flags {
  std::string benchmarks = "fillseq,fillrandom,overwrite,readrandom,readmissing,readseq,stats";
  std::uint64_t num = 1'000'000;
  std::optional<std::uint64_t> reads;  // default: num
  std::size_t value_size = 100;
  int key_digits = 16;
  int threads = 1;
  std::string db = "/tmp/strata_bench";
  std::string sync_method = "default";
  std::uint64_t write_buffer_size = 4u << 20;
  int bloom_bits = 10;
  int batch_size = 1;
  bool use_existing_db = false;
  bool disable_auto_compactions = false;
  bool histogram = true;
  bool mmap_reads = false;
  std::uint64_t seed = 301;
};

[[noreturn]] void Usage(std::string_view bad) {
  std::cerr
      << "strata_bench: bad flag '" << bad << "'\n"
      << "flags: --benchmarks=a,b,c --num=N --reads=N --value_size=N --key_digits=N --threads=N\n"
      << "       --db=PATH --sync_method=default|fsync|fdatasync|fullfsync|barrier\n"
      << "       --write_buffer_size=BYTES --bloom_bits=N --batch_size=N --use_existing_db=0|1\n"
      << "       --disable_auto_compactions=0|1 --histogram=0|1 --mmap_reads=0|1 --seed=N\n";
  std::exit(2);
}

Flags ParseFlags(int argc, char** argv) {
  Flags f;
  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    const auto eq = arg.find('=');
    if (!arg.starts_with("--") || eq == std::string_view::npos) Usage(arg);
    const std::string_view name = arg.substr(2, eq - 2);
    const std::string value(arg.substr(eq + 1));
    auto u64 = [&] { return std::stoull(value); };
    auto boolean = [&] { return value == "1" || value == "true"; };
    if (name == "benchmarks") f.benchmarks = value;
    else if (name == "num") f.num = u64();
    else if (name == "reads") f.reads = u64();
    else if (name == "value_size") f.value_size = static_cast<std::size_t>(u64());
    else if (name == "key_digits") f.key_digits = std::stoi(value);
    else if (name == "threads") f.threads = std::stoi(value);
    else if (name == "db") f.db = value;
    else if (name == "sync_method") f.sync_method = value;
    else if (name == "write_buffer_size") f.write_buffer_size = u64();
    else if (name == "bloom_bits") f.bloom_bits = std::stoi(value);
    else if (name == "batch_size") f.batch_size = std::stoi(value);
    else if (name == "use_existing_db") f.use_existing_db = boolean();
    else if (name == "disable_auto_compactions") f.disable_auto_compactions = boolean();
    else if (name == "histogram") f.histogram = boolean();
    else if (name == "mmap_reads") f.mmap_reads = boolean();
    else if (name == "seed") f.seed = u64();
    else Usage(arg);
  }
  if (f.threads < 1 || f.batch_size < 1) Usage("--threads/--batch_size must be >= 1");
  return f;
}

SyncMethod ParseSyncMethod(std::string_view s) {
  if (s == "default") return kDefaultSyncMethod;
  if (s == "fsync") return SyncMethod::kFsync;
  if (s == "fdatasync") return SyncMethod::kFdatasync;
  if (s == "fullfsync") return SyncMethod::kFullFsync;
  if (s == "barrier") return SyncMethod::kBarrierFsync;
  Usage(s);
}

void Die(std::string_view what, const Status& s) {
  std::cerr << "strata_bench: " << what << ": " << s.ToString() << "\n";
  std::exit(1);
}

std::string Key(std::uint64_t i, int digits) { return std::format("{:0{}}", i, digits); }

// Pre-generated pool of random printable bytes; values are slices of it.
class ValueGen {
 public:
  explicit ValueGen(std::uint64_t seed) {
    Random rnd(seed);
    pool_.resize(1 << 20);
    for (auto& c : pool_) c = static_cast<char>(' ' + rnd.Uniform(95));
  }
  std::string_view Next(std::size_t len) {
    if (pos_ + len > pool_.size()) pos_ = 0;
    std::string_view v(pool_.data() + pos_, len);
    pos_ += len;
    return v;
  }

 private:
  std::string pool_;
  std::size_t pos_ = 0;
};

struct ThreadResult {
  Histogram hist;
  std::uint64_t ops = 0;
  std::uint64_t bytes = 0;
  std::uint64_t found = 0;
};

class Benchmark {
 public:
  explicit Benchmark(Flags flags) : flags_(std::move(flags)) {
    PosixFileSystemOptions fo;
    fo.sync_method = ParseSyncMethod(flags_.sync_method);
    fo.use_mmap_reads = flags_.mmap_reads;
    fs_ = NewPosixFileSystem(fo);
    options_.fs = fs_.get();
    options_.write_buffer_size = flags_.write_buffer_size;
    options_.bloom_bits_per_key = flags_.bloom_bits;
    options_.disable_auto_compactions = flags_.disable_auto_compactions;
  }

  void Run() {
    PrintHeader();
    if (!flags_.use_existing_db) {
      if (Status s = DB::Destroy(options_, flags_.db); !s.ok()) Die("destroy", s);
    }
    Open();
    std::string_view list = flags_.benchmarks;
    while (!list.empty()) {
      const auto comma = list.find(',');
      const std::string_view name = list.substr(0, comma);
      list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
      if (!name.empty()) RunOne(name);
    }
  }

 private:
  using OpFn = void (Benchmark::*)(int thread, ThreadResult& r);

  void PrintHeader() const {
    std::cout << std::format(
        "strata_bench: keys {} bytes, values {} bytes, {} entries, {} thread(s), db {}\n",
        flags_.key_digits, flags_.value_size, flags_.num, flags_.threads, flags_.db);
#if !defined(NDEBUG)
    std::cout << "WARNING: assertions are enabled (debug build); numbers are not meaningful\n";
#endif
    std::cout << "------------------------------------------------------------------\n";
  }

  void Open() {
    options_.create_if_missing = true;
    auto db = DB::Open(options_, flags_.db);
    if (!db) Die("open", db.error());
    db_ = std::move(*db);
  }

  void RunOne(std::string_view name) {
    if (name == "stats") return PrintStats();
    if (name == "compact") {
      const auto t0 = Clock::now();
      if (Status s = db_->CompactRange(std::nullopt, std::nullopt); !s.ok()) Die("compact", s);
      std::cout << std::format("{:<12} : {:.3f} s\n", name, Seconds(Clock::now() - t0));
      return;
    }
    OpFn fn = nullptr;
    WriteOptions wo;
    if (name == "fillseq") fn = &Benchmark::FillSeq;
    else if (name == "fillrandom" || name == "overwrite") fn = &Benchmark::FillRandom;
    else if (name == "fillsync") {
      fn = &Benchmark::FillRandom;
      wo.sync = true;
    } else if (name == "readrandom") fn = &Benchmark::ReadRandom;
    else if (name == "readmissing") fn = &Benchmark::ReadMissing;
    else if (name == "readseq") fn = &Benchmark::ReadSeq;
    else {
      std::cerr << "unknown benchmark '" << name << "'\n";
      return;
    }
    write_options_ = wo;
    RunThreads(name, fn);
    if (name.starts_with("fill") || name == "overwrite") {
      if (Status s = db_->WaitForBackgroundWork(); !s.ok()) Die("wait", s);
    }
  }

  using Clock = std::chrono::steady_clock;
  static double Seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }

  void RunThreads(std::string_view name, OpFn fn) {
    std::vector<ThreadResult> results(static_cast<std::size_t>(flags_.threads));
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};
    Clock::time_point start;
    {
      std::vector<std::thread> threads;
      for (int t = 0; t < flags_.threads; t++) {
        threads.emplace_back([&, t] {
          ready.fetch_add(1);
          while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
          (this->*fn)(t, results[static_cast<std::size_t>(t)]);
        });
      }
      while (ready.load() < flags_.threads) std::this_thread::yield();
      start = Clock::now();
      go.store(true, std::memory_order_release);
      for (auto& th : threads) th.join();
    }
    const double secs = Seconds(Clock::now() - start);
    ThreadResult total;
    for (auto& r : results) {
      total.hist.Merge(r.hist);
      total.ops += r.ops;
      total.bytes += r.bytes;
      total.found += r.found;
    }
    const int active = name == "readseq" ? 1 : flags_.threads;
    const double micros_per_op =
        secs * 1e6 * active / static_cast<double>(std::max<std::uint64_t>(total.ops, 1));
    std::string extra;
    if (total.bytes > 0)
      extra += std::format("  {:7.1f} MB/s", static_cast<double>(total.bytes) / 1048576.0 / secs);
    if (name.starts_with("read") && name != "readseq")
      extra += std::format("  ({} of {} found)", total.found, total.ops);
    std::cout << std::format("{:<12} : {:10.3f} micros/op {:10.0f} ops/sec{}\n", name,
                             micros_per_op, static_cast<double>(total.ops) / secs, extra);
    if (flags_.histogram && total.hist.count() > 0) std::cout << total.hist.Summary() << "\n";
  }

  std::uint64_t PerThread() const {
    return flags_.num / static_cast<std::uint64_t>(flags_.threads);
  }
  std::uint64_t ReadsPerThread() const {
    return flags_.reads.value_or(flags_.num) / static_cast<std::uint64_t>(flags_.threads);
  }

  template <class Op>
  void Timed(ThreadResult& r, Op&& op) {
    const auto t0 = Clock::now();
    op();
    if (flags_.histogram)
      r.hist.Add(static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count()));
  }

  void Write(int thread, ThreadResult& r, bool sequential) {
    Random rnd(flags_.seed + static_cast<std::uint64_t>(thread) * 7919);
    ValueGen values(flags_.seed + static_cast<std::uint64_t>(thread));
    const std::uint64_t n = PerThread();
    const std::uint64_t base = static_cast<std::uint64_t>(thread) * n;
    r.hist.Reserve(static_cast<std::size_t>(n) / static_cast<std::size_t>(flags_.batch_size) + 1);
    WriteBatch batch;
    for (std::uint64_t i = 0; i < n; i += static_cast<std::uint64_t>(flags_.batch_size)) {
      batch.Clear();
      for (int j = 0; j < flags_.batch_size && i + static_cast<std::uint64_t>(j) < n; j++) {
        const std::uint64_t k =
            sequential ? base + i + static_cast<std::uint64_t>(j) : rnd.Uniform(flags_.num);
        const std::string key = Key(k, flags_.key_digits);
        const std::string_view value = values.Next(flags_.value_size);
        batch.Put(key, value);
        r.bytes += key.size() + value.size();
        r.ops++;
      }
      Timed(r, [&] {
        if (Status s = db_->Write(write_options_, batch); !s.ok()) Die("write", s);
      });
    }
  }

  void FillSeq(int thread, ThreadResult& r) { Write(thread, r, true); }
  void FillRandom(int thread, ThreadResult& r) { Write(thread, r, false); }

  void ReadRandom(int thread, ThreadResult& r) { Read(thread, r, false); }
  void ReadMissing(int thread, ThreadResult& r) { Read(thread, r, true); }

  void Read(int thread, ThreadResult& r, bool missing) {
    Random rnd(flags_.seed * 31 + static_cast<std::uint64_t>(thread));
    const std::uint64_t n = ReadsPerThread();
    r.hist.Reserve(static_cast<std::size_t>(n));
    for (std::uint64_t i = 0; i < n; i++) {
      std::string key = Key(rnd.Uniform(flags_.num), flags_.key_digits);
      if (missing) key.push_back('.');  // sorts between real keys, never present
      Timed(r, [&] {
        auto v = db_->Get({}, key);
        if (!v) Die("get", v.error());
        if (v->has_value()) {
          r.found++;
          r.bytes += key.size() + (*v)->size();
        }
      });
      r.ops++;
    }
  }

  void ReadSeq(int thread, ThreadResult& r) {
    if (thread != 0) return;  // one scanner
    auto it = db_->NewIterator({});
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
      r.bytes += it->key().size() + it->value().size();
      r.ops++;
    }
    if (!it->status().ok()) Die("scan", it->status());
  }

  std::uint64_t Counter(std::string_view name) {
    auto p = db_->GetProperty(name);
    return p ? std::stoull(*p) : 0;
  }

  void PrintStats() {
    std::cout << "------------------------------------------------------------------\n";
    if (auto s = db_->GetProperty(properties::kStats)) std::cout << *s << "\n";
    const double user = static_cast<double>(Counter(properties::kUserBytesWritten));
    const double wal = static_cast<double>(Counter(properties::kWalBytesWritten));
    const double flush = static_cast<double>(Counter(properties::kFlushBytesWritten));
    const double comp = static_cast<double>(Counter(properties::kCompactionBytesWritten));
    if (user > 0) {
      std::cout << std::format(
          "write amplification: {:.2f}  (user {:.1f} MB; wal {:.1f} MB, flush {:.1f} MB, "
          "compaction {:.1f} MB)\n",
          (wal + flush + comp) / user, user / 1048576, wal / 1048576, flush / 1048576,
          comp / 1048576);
    }
    std::cout << std::format("wal syncs: {}\n", Counter(properties::kWalSyncs));
    for (int level = 0; level < 7; level++) {
      auto p =
          db_->GetProperty(std::string(properties::kNumFilesAtLevelPrefix) + std::to_string(level));
      if (p && *p != "0") std::cout << std::format("  L{}: {} files\n", level, *p);
    }
  }

  Flags flags_;
  std::unique_ptr<FileSystem> fs_;
  Options options_;
  WriteOptions write_options_;
  std::unique_ptr<DB> db_;
};

}  // namespace
}  // namespace strata::bench

int main(int argc, char** argv) {
  strata::bench::Benchmark b(strata::bench::ParseFlags(argc, argv));
  b.Run();
  return 0;
}
