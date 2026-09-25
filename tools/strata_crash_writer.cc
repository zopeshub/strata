// strata_crash_writer: writes key i = "k%010d" -> Value(i) for i = start,
// start+1, ... forever (or --count), printing "acked <i>" on stdout AFTER
// each write returns OK. crash_test.sh kills it with SIGKILL at a random
// moment; strata_crash_verify then checks what survived.
//
//   --db=PATH --start=N --count=N --sync_every=K (0 = never sync)
//   --value_size=N --write_buffer_size=BYTES
//
// With --sync_every=1, every acked write must survive kill -9 AND power loss.
// With --sync_every=0, acked writes are only in the page cache... and still
// survive kill -9 (the kernel has them). Do you know why? That's the
// difference between process crash and power loss (curriculum/02, 07).

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "strata/db.h"

namespace {

std::string Key(std::uint64_t i) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "k%010llu", static_cast<unsigned long long>(i));
  return buf;
}

std::string Value(std::uint64_t i, std::size_t size) {
  std::string v = "v" + std::to_string(i) + ":";
  while (v.size() < size) v.push_back(static_cast<char>('a' + (i + v.size()) % 26));
  return v;
}

}  // namespace

static int Main(int argc, char** argv) {
  std::string db_path;
  std::uint64_t start = 0;
  std::uint64_t count = ~0ull;
  std::uint64_t sync_every = 1;
  std::size_t value_size = 100;
  std::uint64_t write_buffer_size = 256u << 10;  // small: exercise flushes too
  for (int i = 1; i < argc; i++) {
    const std::string_view a = argv[i];
    const auto eq = a.find('=');
    const std::string_view name = a.substr(0, eq);
    const std::string value(eq == std::string_view::npos ? "" : a.substr(eq + 1));
    if (name == "--db") db_path = value;
    else if (name == "--start") start = std::stoull(value);
    else if (name == "--count") count = std::stoull(value);
    else if (name == "--sync_every") sync_every = std::stoull(value);
    else if (name == "--value_size") value_size = std::stoull(value);
    else if (name == "--write_buffer_size") write_buffer_size = std::stoull(value);
    else {
      std::cerr << "unknown flag " << a << "\n";
      return 2;
    }
  }
  if (db_path.empty()) {
    std::cerr << "usage: strata_crash_writer --db=PATH [--start=N] [--count=N] [--sync_every=K]\n";
    return 2;
  }
  strata::Options options;
  options.create_if_missing = true;
  options.write_buffer_size = write_buffer_size;
  auto db = strata::DB::Open(options, db_path);
  if (!db) {
    std::cerr << "open: " << db.error().ToString() << "\n";
    return 1;
  }
  std::setvbuf(stdout, nullptr, _IOLBF, 0);  // one write(2) per ack line
  for (std::uint64_t i = start; i - start < count; i++) {
    strata::WriteOptions wo;
    wo.sync = sync_every != 0 && (i - start + 1) % sync_every == 0;
    if (auto s = (*db)->Put(wo, Key(i), Value(i, value_size)); !s.ok()) {
      std::cerr << "put " << i << ": " << s.ToString() << "\n";
      return 1;
    }
    if (wo.sync) std::printf("acked %llu\n", static_cast<unsigned long long>(i));
  }
  return 0;
}

// STRATA_TODO throws NotImplementedError: report it instead of aborting.
int main(int argc, char** argv) {
  try {
    return Main(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 3;
  }
}
