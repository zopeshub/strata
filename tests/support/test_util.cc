#include "support/test_util.h"

#include <unistd.h>

#include <cstdlib>
#include <format>
#include <string>
#include <system_error>

namespace strata::testing {

TempDir::TempDir() {
  std::string tmpl = (std::filesystem::temp_directory_path() / "strata-test-XXXXXX").string();
  if (::mkdtemp(tmpl.data()) == nullptr) {
    ADD_FAILURE() << "mkdtemp failed for " << tmpl;
  }
  path_ = tmpl;
}

TempDir::~TempDir() {
  if (std::getenv("STRATA_KEEP_TEST_DIRS") != nullptr) return;
  std::error_code ec;
  std::filesystem::remove_all(path_, ec);
}

std::string NumberedKeyImpl(std::uint64_t i, int width) { return std::format("k{:0{}}", i, width); }

std::string RandomString(Random& rnd, std::size_t len) {
  std::string s(len, '\0');
  for (auto& c : s) c = static_cast<char>(' ' + rnd.Uniform(95));
  return s;
}

std::string RandomBytes(Random& rnd, std::size_t len) {
  std::string s(len, '\0');
  for (auto& c : s) c = static_cast<char>(rnd.Uniform(256));
  return s;
}

std::string Hex(std::string_view bytes) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (const char ch : bytes) {
    const auto c = static_cast<unsigned char>(ch);
    out.push_back(kDigits[c >> 4]);
    out.push_back(kDigits[c & 0xf]);
  }
  return out;
}

std::vector<std::pair<std::string, std::string>> Drain(Iterator& it) {
  std::vector<std::pair<std::string, std::string>> out;
  for (; it.Valid(); it.Next()) out.emplace_back(std::string(it.key()), std::string(it.value()));
  EXPECT_TRUE(it.status().ok()) << it.status().ToString();
  return out;
}

std::map<std::string, std::string> DumpDB(DB& db, const ReadOptions& options) {
  std::map<std::string, std::string> out;
  auto it = db.NewIterator(options);
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    out.emplace(std::string(it->key()), std::string(it->value()));
  }
  EXPECT_TRUE(it->status().ok()) << it->status().ToString();
  return out;
}

// ---------------------------------------------------------------------------
VectorIterator::VectorIterator(std::vector<std::pair<std::string, std::string>> entries,
                               const Comparator& cmp)
    : entries_(std::move(entries)), cmp_(cmp), pos_(entries_.size()) {}

void VectorIterator::InjectErrorAt(std::size_t index, Status status) {
  error_index_ = index;
  error_ = std::move(status);
}

void VectorIterator::CheckError() {
  if (pos_ == error_index_) {
    status_ = error_;
    pos_ = entries_.size();
  }
}

bool VectorIterator::Valid() const { return pos_ < entries_.size(); }

void VectorIterator::SeekToFirst() {
  if (!status_.ok()) return;
  pos_ = 0;
  CheckError();
}

void VectorIterator::Seek(std::string_view target) {
  if (!status_.ok()) return;
  pos_ = 0;
  while (pos_ < entries_.size() && cmp_.Compare(entries_[pos_].first, target) < 0) ++pos_;
  CheckError();
}

void VectorIterator::Next() {
  ++pos_;
  CheckError();
}

std::string_view VectorIterator::key() const { return entries_[pos_].first; }
std::string_view VectorIterator::value() const { return entries_[pos_].second; }
Status VectorIterator::status() const { return status_; }

}  // namespace strata::testing
