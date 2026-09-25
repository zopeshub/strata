#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace strata {

class FileSystem;
class Snapshot;

// How WritableFile::Sync() makes data durable. See curriculum/07-fsync-semantics.md.
enum class SyncMethod : std::uint8_t {
  kFsync,         // fsync(2). On macOS this does NOT flush the drive's write cache.
  kFdatasync,     // fdatasync(2) on Linux; falls back to fsync(2) elsewhere.
  kFullFsync,     // macOS: fcntl(F_FULLFSYNC) (true durability). Linux: fsync(2).
  kBarrierFsync,  // macOS: fcntl(F_BARRIERFSYNC) (ordering, not durability). Linux: fsync(2).
};

#if defined(__APPLE__)
inline constexpr SyncMethod kDefaultSyncMethod = SyncMethod::kFullFsync;
#else
inline constexpr SyncMethod kDefaultSyncMethod = SyncMethod::kFdatasync;
#endif

struct Options {
  // File system to use. nullptr means DefaultFileSystem() (POSIX).
  // Not owned; must outlive the DB.
  FileSystem* fs = nullptr;

  bool create_if_missing = true;
  bool error_if_exists = false;

  // If true, Open() fails on any WAL/MANIFEST corruption instead of treating
  // a torn tail as the end of the log.
  bool paranoid_checks = false;

  // --- Write path -----------------------------------------------------------
  // Memtable size at which it becomes immutable and is flushed to L0.
  std::size_t write_buffer_size = 4u << 20;

  // --- SSTable format -------------------------------------------------------
  std::size_t block_size = 4u << 10;  // uncompressed data block target size
  int block_restart_interval = 16;    // keys between restart points
  int bloom_bits_per_key = 10;        // 0 disables bloom filters

  // --- Leveled compaction ---------------------------------------------------
  int l0_compaction_trigger = 4;       // start compacting L0 at this many files
  int l0_slowdown_writes_trigger = 8;  // delay each write by ~1ms
  int l0_stop_writes_trigger = 12;     // block writers until compaction catches up
  std::uint64_t target_file_size = 2u << 20;
  std::uint64_t max_bytes_for_level_base = 10u << 20;  // L1 size limit
  int max_bytes_for_level_multiplier = 10;

  // Tests and benchmarks can turn off automatic background compaction and
  // drive it explicitly with DB::CompactRange(). This also disables the L0
  // slowdown/stop write stalls (nothing would ever un-stall them).
  // Memtable flushes still happen automatically.
  bool disable_auto_compactions = false;
};

struct WriteOptions {
  // If true, the write is durable (WAL synced) before Put/Write returns.
  bool sync = false;
};

struct ReadOptions {
  // Verify block checksums on every read (not just on open).
  bool verify_checksums = false;
  // Read as of this snapshot; nullptr means "latest".
  std::shared_ptr<const Snapshot> snapshot;
};

}  // namespace strata
