#include "support/mem_fs.h"

#include <algorithm>
#include <cstring>
#include <thread>
#include <utility>

#include "util/random.h"

namespace strata::testing {

struct MemFileSystem::Inode {
  std::string data;     // live contents (what reads see)
  std::string durable;  // contents that survive power loss
};

struct MemFileSystem::State {
  mutable std::mutex mu;
  std::map<std::string, std::shared_ptr<Inode>> live;
  std::map<std::string, std::shared_ptr<Inode>> durable;
  std::set<std::string> dirs;
  std::set<std::string> locks;
  bool frozen = false;
  int fail_syncs = 0;
  std::chrono::microseconds sync_latency{0};
  std::uint64_t file_syncs = 0;
  std::uint64_t dir_syncs = 0;
};

namespace {
Status Frozen(std::string_view op) {
  return Status::IOError(op, "file system is frozen (simulated crash)");
}
}  // namespace

std::string MemFileSystem::Key(const Path& p) {
  std::string s = p.lexically_normal().generic_string();
  while (s.size() > 1 && s.back() == '/') s.pop_back();
  return s;
}

std::string MemFileSystem::Parent(const std::string& key) {
  return Path(key).parent_path().generic_string();
}

// ---------------------------------------------------------------------------
// File objects
// ---------------------------------------------------------------------------
class MemFileSystem::WFile final : public WritableFile {
 public:
  // REQUIRES: st->mu held by the caller (initial size is read from the inode).
  WFile(std::shared_ptr<State> st, std::shared_ptr<Inode> node, std::string name)
      : st_(std::move(st)),
        node_(std::move(node)),
        name_(std::move(name)),
        size_(node_->data.size()) {}

  Status Append(std::string_view data) override {
    std::lock_guard l(st_->mu);
    if (closed_) return Status::IOError(name_, "append after close");
    if (st_->frozen) return Frozen(name_);
    node_->data.append(data);
    size_ += data.size();
    return Status::OK();
  }

  Status Flush() override {
    std::lock_guard l(st_->mu);
    if (st_->frozen) return Frozen(name_);
    return Status::OK();
  }

  Status Sync() override {
    std::chrono::microseconds latency;
    {
      std::lock_guard l(st_->mu);
      latency = st_->sync_latency;
    }
    if (latency.count() > 0) std::this_thread::sleep_for(latency);
    std::lock_guard l(st_->mu);
    if (closed_) return Status::IOError(name_, "sync after close");
    if (st_->frozen) return Frozen(name_);
    if (st_->fail_syncs != 0) {
      if (st_->fail_syncs > 0) --st_->fail_syncs;
      return Status::IOError(name_, "injected fsync failure");
    }
    node_->durable = node_->data;
    ++st_->file_syncs;
    return Status::OK();
  }

  Status Close() override {
    std::lock_guard l(st_->mu);
    closed_ = true;
    return Status::OK();
  }

  std::uint64_t Size() const override { return size_; }

 private:
  std::shared_ptr<State> st_;
  std::shared_ptr<Inode> node_;
  std::string name_;
  std::uint64_t size_ = 0;
  bool closed_ = false;
};

class MemFileSystem::SFile final : public SequentialFile {
 public:
  SFile(std::shared_ptr<State> st, std::shared_ptr<Inode> node)
      : st_(std::move(st)), node_(std::move(node)) {}

  Result<std::string_view> Read(std::span<char> scratch) override {
    std::lock_guard l(st_->mu);
    const std::string& d = node_->data;
    if (pos_ >= d.size()) return std::string_view{};
    const std::size_t n = std::min<std::size_t>(scratch.size(), d.size() - pos_);
    std::memcpy(scratch.data(), d.data() + pos_, n);
    pos_ += n;
    return std::string_view(scratch.data(), n);
  }

  Status Skip(std::uint64_t n) override {
    std::lock_guard l(st_->mu);
    pos_ = std::min<std::uint64_t>(node_->data.size(), pos_ + n);
    return Status::OK();
  }

 private:
  std::shared_ptr<State> st_;
  std::shared_ptr<Inode> node_;
  std::uint64_t pos_ = 0;
};

class MemFileSystem::RFile final : public RandomAccessFile {
 public:
  RFile(std::shared_ptr<State> st, std::shared_ptr<Inode> node)
      : st_(std::move(st)), node_(std::move(node)) {}

  Result<std::string_view> Read(std::uint64_t offset, std::span<char> scratch) const override {
    std::lock_guard l(st_->mu);
    const std::string& d = node_->data;
    if (offset >= d.size()) return std::string_view{};
    const std::size_t n = std::min<std::size_t>(scratch.size(), d.size() - offset);
    std::memcpy(scratch.data(), d.data() + offset, n);
    return std::string_view(scratch.data(), n);
  }

  std::uint64_t Size() const override {
    std::lock_guard l(st_->mu);
    return node_->data.size();
  }

 private:
  std::shared_ptr<State> st_;
  std::shared_ptr<Inode> node_;
};

class MemFileSystem::Lock final : public FileLock {
 public:
  Lock(std::shared_ptr<State> st, std::string key) : st_(std::move(st)), key_(std::move(key)) {}
  ~Lock() override {
    std::lock_guard l(st_->mu);
    st_->locks.erase(key_);
  }

 private:
  std::shared_ptr<State> st_;
  std::string key_;
};

// ---------------------------------------------------------------------------
// FileSystem
// ---------------------------------------------------------------------------
MemFileSystem::MemFileSystem() : state_(std::make_shared<State>()) { state_->dirs.insert("/"); }

MemFileSystem::~MemFileSystem() = default;

Result<std::unique_ptr<WritableFile>> MemFileSystem::NewWritableFile(const Path& path) {
  const std::string key = Key(path);
  std::lock_guard l(state_->mu);
  if (state_->frozen) return Err(Frozen(key));
  if (!state_->dirs.contains(Parent(key))) return Err(Status::NotFound(key, "no such directory"));
  auto& slot = state_->live[key];
  if (!slot) {
    slot = std::make_shared<Inode>();
  } else {
    slot->data.clear();  // O_TRUNC keeps the inode; durable contents untouched
  }
  auto node = slot;
  return std::unique_ptr<WritableFile>(std::make_unique<WFile>(state_, std::move(node), key));
}

Result<std::unique_ptr<WritableFile>> MemFileSystem::NewAppendableFile(const Path& path) {
  const std::string key = Key(path);
  std::lock_guard l(state_->mu);
  if (state_->frozen) return Err(Frozen(key));
  if (!state_->dirs.contains(Parent(key))) return Err(Status::NotFound(key, "no such directory"));
  auto& slot = state_->live[key];
  if (!slot) slot = std::make_shared<Inode>();
  auto node = slot;
  return std::unique_ptr<WritableFile>(std::make_unique<WFile>(state_, std::move(node), key));
}

Result<std::unique_ptr<SequentialFile>> MemFileSystem::NewSequentialFile(const Path& path) {
  const std::string key = Key(path);
  std::lock_guard l(state_->mu);
  auto it = state_->live.find(key);
  if (it == state_->live.end()) return Err(Status::NotFound(key));
  return std::unique_ptr<SequentialFile>(std::make_unique<SFile>(state_, it->second));
}

Result<std::unique_ptr<RandomAccessFile>> MemFileSystem::NewRandomAccessFile(const Path& path) {
  const std::string key = Key(path);
  std::lock_guard l(state_->mu);
  auto it = state_->live.find(key);
  if (it == state_->live.end()) return Err(Status::NotFound(key));
  return std::unique_ptr<RandomAccessFile>(std::make_unique<RFile>(state_, it->second));
}

bool MemFileSystem::FileExists(const Path& path) {
  const std::string key = Key(path);
  std::lock_guard l(state_->mu);
  return state_->live.contains(key) || state_->dirs.contains(key);
}

Result<std::vector<std::string>> MemFileSystem::GetChildren(const Path& dir) {
  const std::string key = Key(dir);
  std::lock_guard l(state_->mu);
  if (!state_->dirs.contains(key)) return Err(Status::NotFound(key, "no such directory"));
  std::vector<std::string> out;
  for (const auto& [name, node] : state_->live) {
    if (Parent(name) == key) out.push_back(Path(name).filename().string());
  }
  for (const auto& d : state_->dirs) {
    if (d != key && Parent(d) == key) out.push_back(Path(d).filename().string());
  }
  return out;
}

Result<std::uint64_t> MemFileSystem::GetFileSize(const Path& path) {
  const std::string key = Key(path);
  std::lock_guard l(state_->mu);
  auto it = state_->live.find(key);
  if (it == state_->live.end()) return Err(Status::NotFound(key));
  return static_cast<std::uint64_t>(it->second->data.size());
}

Status MemFileSystem::RemoveFile(const Path& path) {
  const std::string key = Key(path);
  std::lock_guard l(state_->mu);
  if (state_->frozen) return Frozen(key);
  if (state_->live.erase(key) == 0) return Status::NotFound(key);
  return Status::OK();
}

Status MemFileSystem::RenameFile(const Path& from, const Path& to) {
  const std::string src = Key(from);
  const std::string dst = Key(to);
  std::lock_guard l(state_->mu);
  if (state_->frozen) return Frozen(src);
  auto it = state_->live.find(src);
  if (it == state_->live.end()) return Status::NotFound(src);
  if (!state_->dirs.contains(Parent(dst))) return Status::NotFound(dst, "no such directory");
  auto node = it->second;
  state_->live.erase(it);
  state_->live[dst] = std::move(node);
  return Status::OK();
}

Status MemFileSystem::CreateDir(const Path& dir) {
  const std::string key = Key(dir);
  std::lock_guard l(state_->mu);
  if (state_->frozen) return Frozen(key);
  if (state_->live.contains(key)) return Status::IOError(key, "exists and is a file");
  // Create missing parents too (like `mkdir -p`); simplifies temp-dir setup.
  Path p(key);
  std::vector<std::string> chain;
  for (Path cur = p; !cur.empty() && cur != cur.root_path(); cur = cur.parent_path()) {
    chain.push_back(cur.generic_string());
  }
  for (auto& d : chain) state_->dirs.insert(d);
  return Status::OK();
}

Status MemFileSystem::RemoveDir(const Path& dir) {
  const std::string key = Key(dir);
  std::lock_guard l(state_->mu);
  if (state_->frozen) return Frozen(key);
  if (!state_->dirs.contains(key)) return Status::NotFound(key);
  for (const auto& [name, node] : state_->live) {
    if (Parent(name) == key) return Status::IOError(key, "directory not empty");
  }
  state_->dirs.erase(key);
  return Status::OK();
}

Status MemFileSystem::SyncDir(const Path& dir) {
  const std::string key = Key(dir);
  std::lock_guard l(state_->mu);
  if (state_->frozen) return Frozen(key);
  if (!state_->dirs.contains(key)) return Status::NotFound(key);
  std::erase_if(state_->durable, [&](const auto& kv) { return Parent(kv.first) == key; });
  for (const auto& [name, node] : state_->live) {
    if (Parent(name) == key) state_->durable[name] = node;
  }
  ++state_->dir_syncs;
  return Status::OK();
}

Result<std::unique_ptr<FileLock>> MemFileSystem::LockFile(const Path& path) {
  const std::string key = Key(path);
  std::lock_guard l(state_->mu);
  if (state_->locks.contains(key)) return Err(Status::Busy(key, "lock held"));
  if (!state_->dirs.contains(Parent(key))) return Err(Status::NotFound(key, "no such directory"));
  if (!state_->live.contains(key)) {
    if (state_->frozen) return Err(Frozen(key));
    state_->live[key] = std::make_shared<Inode>();
  }
  state_->locks.insert(key);
  return std::unique_ptr<FileLock>(std::make_unique<Lock>(state_, key));
}

// ---------------------------------------------------------------------------
// Crash simulation and fault injection
// ---------------------------------------------------------------------------
void MemFileSystem::SimulatePowerLoss(Unsynced mode, std::uint64_t seed) {
  std::lock_guard l(state_->mu);
  Random rnd(seed);
  // Collect every inode reachable from either view, revert each exactly once.
  std::set<Inode*> seen;
  auto revert = [&](const std::shared_ptr<Inode>& node) {
    if (!seen.insert(node.get()).second) return;
    const bool extends = node->data.size() > node->durable.size() &&
                         node->data.compare(0, node->durable.size(), node->durable) == 0;
    if (mode == Unsynced::kKeepRandomPrefix && extends) {
      const std::size_t tail = node->data.size() - node->durable.size();
      const std::size_t keep = static_cast<std::size_t>(rnd.Uniform(tail + 1));
      node->data.resize(node->durable.size() + keep);
      node->durable = node->data;
    } else {
      node->data = node->durable;
    }
  };
  for (auto& [name, node] : state_->live) revert(node);
  for (auto& [name, node] : state_->durable) revert(node);
  state_->live = state_->durable;
  state_->locks.clear();
}

void MemFileSystem::Freeze() {
  std::lock_guard l(state_->mu);
  state_->frozen = true;
}

void MemFileSystem::Unfreeze() {
  std::lock_guard l(state_->mu);
  state_->frozen = false;
}

bool MemFileSystem::frozen() const {
  std::lock_guard l(state_->mu);
  return state_->frozen;
}

void MemFileSystem::FailNextSyncs(int n) {
  std::lock_guard l(state_->mu);
  state_->fail_syncs = n;
}

void MemFileSystem::SetSyncLatency(std::chrono::microseconds latency) {
  std::lock_guard l(state_->mu);
  state_->sync_latency = latency;
}

std::uint64_t MemFileSystem::file_sync_count() const {
  std::lock_guard l(state_->mu);
  return state_->file_syncs;
}

std::uint64_t MemFileSystem::dir_sync_count() const {
  std::lock_guard l(state_->mu);
  return state_->dir_syncs;
}

Result<std::string> MemFileSystem::ReadFileToString(const Path& path) {
  const std::string key = Key(path);
  std::lock_guard l(state_->mu);
  auto it = state_->live.find(key);
  if (it == state_->live.end()) return Err(Status::NotFound(key));
  return it->second->data;
}

Status MemFileSystem::WriteStringToFile(const Path& path, std::string_view contents, bool sync) {
  auto file = NewWritableFile(path);
  if (!file) return file.error();
  STRATA_RETURN_IF_ERROR((*file)->Append(contents));
  if (sync) STRATA_RETURN_IF_ERROR((*file)->Sync());
  return (*file)->Close();
}

Status MemFileSystem::CorruptByte(const Path& path, std::uint64_t offset, unsigned char xor_mask) {
  const std::string key = Key(path);
  std::lock_guard l(state_->mu);
  auto it = state_->live.find(key);
  if (it == state_->live.end()) return Status::NotFound(key);
  auto& d = it->second->data;
  if (offset >= d.size()) return Status::InvalidArgument(key, "offset past end");
  d[offset] = static_cast<char>(static_cast<unsigned char>(d[offset]) ^ xor_mask);
  it->second->durable = d;
  return Status::OK();
}

Status MemFileSystem::Truncate(const Path& path, std::uint64_t size) {
  const std::string key = Key(path);
  std::lock_guard l(state_->mu);
  auto it = state_->live.find(key);
  if (it == state_->live.end()) return Status::NotFound(key);
  auto& d = it->second->data;
  if (size < d.size()) d.resize(size);
  it->second->durable = d;
  return Status::OK();
}

}  // namespace strata::testing
