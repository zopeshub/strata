// strata_sst_dump: print an SSTable written by the DB (internal keys).
//
//   strata_sst_dump /tmp/demo/000007.sst           every entry
//   strata_sst_dump --stats /tmp/demo/000007.sst   properties only
//   strata_sst_dump --get=<user_key> FILE          point lookup (shows bloom use)
//   strata_sst_dump --build=N FILE                 write a table of N entries
//                                                  ("key00000000", "key00000002", ...)
//                                                  with YOUR TableBuilder (M4 demo,
//                                                  before the DB can flush)

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include "db/dbformat.h"
#include "strata/file_system.h"
#include "table/format.h"
#include "table/table.h"
#include "table/table_builder.h"

namespace {

int Build(const char* path, std::uint64_t n, const strata::TableOptions& topts) {
  auto file = strata::DefaultFileSystem()->NewWritableFile(path);
  if (!file) {
    std::cerr << file.error().ToString() << "\n";
    return 1;
  }
  strata::TableBuilder builder(topts, **file);
  char key[32];
  for (std::uint64_t i = 0; i < n; i++) {
    std::snprintf(key, sizeof(key), "key%08llu", static_cast<unsigned long long>(2 * i));
    const std::string ikey = strata::MakeInternalKey(key, i + 1, strata::ValueType::kValue);
    builder.Add(ikey, "value-" + std::to_string(i));
  }
  strata::Status s = builder.Finish();
  if (s.ok()) s = (*file)->Sync();
  if (s.ok()) s = (*file)->Close();
  if (!s.ok()) {
    std::cerr << "build: " << s.ToString() << "\n";
    return 1;
  }
  std::cout
      << "wrote " << n << " entries to " << path << " (" << builder.FileSize() << " bytes)\n"
      << "keys are key00000000, key00000002, ...: try --get=key00000002 and --get=key00000001\n";
  return 0;
}

std::string Printable(std::string_view s) {
  std::string out;
  for (char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    if (c >= 0x20 && c < 0x7f) {
      out.push_back(ch);
    } else {
      static constexpr char kHex[] = "0123456789abcdef";
      out += "\\x";
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0xf]);
    }
  }
  return out;
}

}  // namespace

static int Main(int argc, char** argv) {
  bool stats_only = false;
  std::string get_key;
  bool do_get = false;
  std::uint64_t build_n = 0;
  const char* path = nullptr;
  for (int i = 1; i < argc; i++) {
    const std::string_view a = argv[i];
    if (a == "--stats") {
      stats_only = true;
    } else if (a.starts_with("--build=")) {
      build_n = std::stoull(std::string(a.substr(8)));
    } else if (a.starts_with("--get=")) {
      do_get = true;
      get_key = std::string(a.substr(6));
    } else {
      path = argv[i];
    }
  }
  if (path == nullptr) {
    std::cerr << "usage: strata_sst_dump [--stats] [--get=KEY] [--build=N] <file.sst>\n";
    return 2;
  }
  static const strata::InternalKeyComparator icmp;
  strata::TableOptions topts;
  topts.comparator = &icmp;
  if (build_n > 0) return Build(path, build_n, topts);

  strata::FileSystem* fs = strata::DefaultFileSystem();
  auto size = fs->GetFileSize(path);
  auto file = fs->NewRandomAccessFile(path);
  if (!size || !file) {
    std::cerr << (size ? file.error() : size.error()).ToString() << "\n";
    return 1;
  }
  auto table = strata::Table::Open(topts, std::move(*file), *size);
  if (!table) {
    std::cerr << "open: " << table.error().ToString() << "\n";
    return 1;
  }
  const auto& p = (*table)->properties();
  std::cout << "file size:        " << *size << "\n"
            << "entries:          " << p.num_entries << "\n"
            << "data blocks:      " << p.num_data_blocks << "\n"
            << "raw key bytes:    " << p.raw_key_bytes << "\n"
            << "raw value bytes:  " << p.raw_value_bytes << "\n"
            << "index block:      " << p.index_block_size << " bytes\n"
            << "filter block:     " << p.filter_block_size << " bytes\n";
  if (stats_only) return 0;

  strata::ReadOptions ro;
  ro.verify_checksums = true;
  if (do_get) {
    const strata::LookupKey lk(get_key, strata::kMaxSequenceNumber);
    auto r = (*table)->Get(ro, lk);
    if (!r) {
      std::cerr << "get: " << r.error().ToString() << "\n";
      return 1;
    }
    if (auto* v = std::get_if<std::string>(&*r)) {
      std::cout << "found: " << Printable(*v) << "\n";
    } else if (std::holds_alternative<strata::KeyDeleted>(*r)) {
      std::cout << "deleted (tombstone)\n";
    } else {
      std::cout << "absent\n";
    }
    const auto st = (*table)->stats();
    std::cout << "data blocks read: " << st.data_blocks_read
              << ", bloom checks: " << st.bloom_checks
              << ", bloom negatives: " << st.bloom_negatives << "\n";
    return 0;
  }
  auto it = (*table)->NewIterator(ro);
  std::uint64_t n = 0;
  for (it->SeekToFirst(); it->Valid(); it->Next(), n++) {
    auto parsed = strata::ParseInternalKey(it->key());
    if (!parsed) {
      std::cout << "  !! unparsable internal key: " << Printable(it->key()) << "\n";
      continue;
    }
    std::cout << "'" << Printable(parsed->user_key) << "' @ " << parsed->sequence << " : "
              << (parsed->type == strata::ValueType::kValue ? "PUT '" + Printable(it->value()) + "'"
                                                            : "DEL")
              << "\n";
  }
  if (!it->status().ok()) {
    std::cerr << "iteration: " << it->status().ToString() << "\n";
    return 1;
  }
  std::cout << n << " entries\n";
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
