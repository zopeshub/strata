// strata_wal_dump: print every record in a WAL (or MANIFEST) file.
//
//   strata_wal_dump /tmp/demo/000003.log            decoded write batches
//   strata_wal_dump --raw /tmp/demo/MANIFEST-000002 record offsets and sizes only
//   strata_wal_dump --no-verify FILE                skip CRC checks
//
// Corrupted regions are reported, not fatal: exactly what recovery sees.

#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

#include "db/log_reader.h"
#include "strata/file_system.h"
#include "strata/write_batch.h"

namespace {

std::string Printable(std::string_view s, std::size_t max = 60) {
  std::string out;
  for (std::size_t i = 0; i < s.size() && i < max; i++) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c >= 0x20 && c < 0x7f) {
      out.push_back(static_cast<char>(c));
    } else {
      static constexpr char kHex[] = "0123456789abcdef";
      out += "\\x";
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0xf]);
    }
  }
  if (s.size() > max) out += "...(" + std::to_string(s.size()) + " bytes)";
  return out;
}

}  // namespace

static int Main(int argc, char** argv) {
  bool raw = false;
  bool verify = true;
  const char* path = nullptr;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--raw") == 0) {
      raw = true;
    } else if (std::strcmp(argv[i], "--no-verify") == 0) {
      verify = false;
    } else {
      path = argv[i];
    }
  }
  if (path == nullptr) {
    std::cerr << "usage: strata_wal_dump [--raw] [--no-verify] <file>\n";
    return 2;
  }
  auto file = strata::DefaultFileSystem()->NewSequentialFile(path);
  if (!file) {
    std::cerr << file.error().ToString() << "\n";
    return 1;
  }
  std::uint64_t dropped = 0;
  strata::log::Reader reader(
      std::move(*file),
      [&](std::size_t bytes, const strata::Status& reason) {
        dropped += bytes;
        std::cout << "  !! dropped " << bytes << " bytes: " << reason.ToString() << "\n";
      },
      verify);

  std::string scratch;
  std::uint64_t records = 0;
  while (auto rec = reader.ReadRecord(scratch)) {
    records++;
    std::cout << "@" << std::setw(10) << reader.LastRecordOffset() << "  record " << records << ", "
              << rec->size() << " bytes\n";
    if (raw) continue;
    auto batch = strata::WriteBatch::FromContents(*rec);
    if (!batch) {
      std::cout << "    (not a write batch: " << batch.error().ToString() << ")\n";
      continue;
    }
    std::cout << "    seq " << batch->Sequence() << ", " << batch->Count() << " op(s)\n";
    const strata::Status s = batch->ForEach([](const strata::BatchOp& op) {
      std::visit(strata::Overloaded{
                     [](const strata::PutOp& p) {
                       std::cout << "      PUT " << Printable(p.key) << " = " << Printable(p.value)
                                 << "\n";
                     },
                     [](const strata::DeleteOp& d) {
                       std::cout << "      DEL " << Printable(d.key) << "\n";
                     },
                 },
                 op);
    });
    if (!s.ok()) std::cout << "    !! " << s.ToString() << "\n";
  }
  std::cout << records << " record(s), " << dropped << " byte(s) dropped\n";
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
