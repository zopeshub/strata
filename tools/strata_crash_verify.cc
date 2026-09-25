// strata_crash_verify: after strata_crash_writer was killed, check that
//   1. every key <= --acked is present with the right value (durability)
//   2. the present keys form a contiguous prefix 0..n-1 (no holes: the
//      prefix property; a hole means records were applied out of order or
//      a corrupt record was skipped and later ones were replayed)
//   3. nothing is present that was never written
// Prints "next=<n>" (where the writer should resume) and exits 0 if OK.
//
//   strata_crash_verify --db=PATH --acked=N [--value_size=100]

#include <cstdio>
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
  long long acked = -1;
  std::size_t value_size = 100;
  for (int i = 1; i < argc; i++) {
    const std::string_view a = argv[i];
    const auto eq = a.find('=');
    const std::string_view name = a.substr(0, eq);
    const std::string value(eq == std::string_view::npos ? "" : a.substr(eq + 1));
    if (name == "--db") db_path = value;
    else if (name == "--acked") acked = std::stoll(value);
    else if (name == "--value_size") value_size = std::stoull(value);
    else {
      std::cerr << "unknown flag " << a << "\n";
      return 2;
    }
  }
  strata::Options options;
  auto db = strata::DB::Open(options, db_path);
  if (!db) {
    std::cerr << "FAIL: open after crash: " << db.error().ToString() << "\n";
    return 1;
  }
  // Point reads only, so this works from M2 on (no DB iterator until M5).
  std::uint64_t expected = 0;
  for (;; expected++) {
    auto r = (*db)->Get({}, Key(expected));
    if (!r) {
      std::cerr << "FAIL: get " << Key(expected) << ": " << r.error().ToString() << "\n";
      return 1;
    }
    if (!r->has_value()) break;
    if (**r != Value(expected, value_size)) {
      std::cerr << "FAIL: wrong value for " << Key(expected) << "\n";
      return 1;
    }
  }
  // Holes: after the first missing key, nothing further may exist. (The
  // writer never gets far ahead of the crash point; 100k covers it.)
  for (std::uint64_t i = expected + 1; i < expected + 100'000; i++) {
    auto r = (*db)->Get({}, Key(i));
    if (r && r->has_value()) {
      std::cerr << "FAIL: hole: " << Key(expected) << " is missing but " << Key(i) << " survived\n";
      return 1;
    }
  }
  if (acked >= 0 && expected <= static_cast<std::uint64_t>(acked)) {
    std::cerr << "FAIL: lost acknowledged writes: have 0.." << static_cast<long long>(expected) - 1
              << ", acked through " << acked << "\n";
    return 1;
  }
  std::cout << "next=" << expected << "\n";
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
