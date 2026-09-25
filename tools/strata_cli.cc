// strata_cli: a tiny shell over the public API. Handy for demos.
//
//   $ strata_cli /tmp/demo
//   strata> put name ada
//   strata> get name
//   ada
//   strata> scan
//   strata> help

#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "strata/db.h"
#include "strata/range.h"

namespace {

void Help() {
  std::cout << "commands:\n"
               "  put <key> <value>       sync=false write\n"
               "  sput <key> <value>      sync=true write\n"
               "  get <key>\n"
               "  del <key>\n"
               "  scan [from] [to]        up to 100 entries of [from, to)\n"
               "  fill <n> [prefix]       n puts: <prefix>000000.. -> value\n"
               "  flush | compact | wait\n"
               "  prop <name>             e.g. prop strata.stats, prop strata.files\n"
               "  snap                    take a snapshot; later reads: 'get@ <key>'\n"
               "  get@ <key>              read at the last snapshot\n"
               "  help | quit\n";
}

std::vector<std::string> Split(const std::string& line) {
  std::istringstream in(line);
  std::vector<std::string> out;
  for (std::string w; in >> w;) out.push_back(w);
  return out;
}

}  // namespace

static int Main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: strata_cli <db-dir>\n";
    return 2;
  }
  strata::Options options;
  options.create_if_missing = true;
  auto opened = strata::DB::Open(options, argv[1]);
  if (!opened) {
    std::cerr << "open: " << opened.error().ToString() << "\n";
    return 1;
  }
  std::unique_ptr<strata::DB> db = std::move(*opened);
  std::shared_ptr<const strata::Snapshot> snapshot;

  auto report = [](const strata::Status& s) {
    if (!s.ok()) std::cout << "error: " << s.ToString() << "\n";
  };
  auto print_get = [&](const strata::ReadOptions& ro, const std::string& key) {
    auto r = db->Get(ro, key);
    if (!r) return report(r.error());
    std::cout << (r->has_value() ? **r : "(not found)") << "\n";
  };

  std::string line;
  while (std::cout << "strata> " << std::flush, std::getline(std::cin, line)) {
    const auto w = Split(line);
    if (w.empty()) continue;
    const std::string& cmd = w[0];
    try {
      if (cmd == "quit" || cmd == "exit") break;
      if (cmd == "help") {
        Help();
      } else if ((cmd == "put" || cmd == "sput") && w.size() == 3) {
        strata::WriteOptions wo;
        wo.sync = cmd == "sput";
        report(db->Put(wo, w[1], w[2]));
      } else if (cmd == "get" && w.size() == 2) {
        print_get({}, w[1]);
      } else if (cmd == "get@" && w.size() == 2) {
        if (!snapshot) {
          std::cout << "no snapshot; use 'snap' first\n";
          continue;
        }
        strata::ReadOptions ro;
        ro.snapshot = snapshot;
        print_get(ro, w[1]);
      } else if (cmd == "del" && w.size() == 2) {
        report(db->Delete({}, w[1]));
      } else if (cmd == "scan" && w.size() <= 3) {
        auto it = db->NewIterator({});
        if (w.size() >= 2) {
          it->Seek(w[1]);
        } else {
          it->SeekToFirst();
        }
        int n = 0;
        for (; it->Valid() && n < 100; it->Next(), n++) {
          if (w.size() == 3 && it->key() >= w[2]) break;
          std::cout << it->key() << " -> " << it->value() << "\n";
        }
        report(it->status());
      } else if (cmd == "fill" && (w.size() == 2 || w.size() == 3)) {
        const int n = std::stoi(w[1]);
        const std::string prefix = w.size() == 3 ? w[2] : "key";
        for (int i = 0; i < n; i++) {
          std::string k = prefix +
                          std::string(6 - std::min<std::size_t>(6, std::to_string(i).size()), '0') +
                          std::to_string(i);
          if (auto s = db->Put({}, k, "value-" + std::to_string(i)); !s.ok()) {
            report(s);
            break;
          }
        }
      } else if (cmd == "flush") {
        report(db->Flush());
      } else if (cmd == "compact") {
        report(db->CompactRange(std::nullopt, std::nullopt));
      } else if (cmd == "wait") {
        report(db->WaitForBackgroundWork());
      } else if (cmd == "prop" && w.size() == 2) {
        auto p = db->GetProperty(w[1]);
        std::cout << (p ? *p : "(unknown property)") << "\n";
      } else if (cmd == "snap") {
        snapshot = db->GetSnapshot();
        std::cout << "snapshot at sequence " << snapshot->sequence() << "\n";
      } else {
        std::cout << "bad command; try 'help'\n";
      }
    } catch (const std::exception& e) {
      // STRATA_TODO throws NotImplementedError: show it instead of dying.
      std::cout << e.what() << "\n";
    }
  }
  snapshot.reset();
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
