#pragma once

// Internal key format and ordering. (M2)
//
// Every entry stored anywhere in the engine (memtable, SSTables) is keyed by
// an INTERNAL key, which appends an 8-byte trailer to the user key:
//
//   internal_key := user_key | fixed64( (sequence << 8) | type )
//
// Ordering (InternalKeyComparator):
//   1. user_key ascending (bytewise)
//   2. sequence DEScending   <- newest version first
//   3. type descending
//
// So a Seek(user_key, snapshot_seq) lands on the newest version of user_key
// that is visible at snapshot_seq. That single property is what makes MVCC
// snapshots, tombstones, and "newest wins" merges fall out naturally. Make
// sure you can explain why in one sentence (checkpoint question in M2).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace strata {

using SequenceNumber = std::uint64_t;

// 56 bits: the low 8 bits of the packed trailer hold the type.
inline constexpr SequenceNumber kMaxSequenceNumber = (SequenceNumber{1} << 56) - 1;

inline constexpr int kNumLevels = 7;

enum class ValueType : std::uint8_t {
  kDeletion = 0x0,
  kValue = 0x1,
};

// When seeking, we want the newest entry for a user key at or below a
// sequence. Because type sorts descending, seeking with the HIGHEST type
// value finds all entries of that sequence.
inline constexpr ValueType kValueTypeForSeek = ValueType::kValue;

struct ParsedInternalKey {
  std::string_view user_key;
  SequenceNumber sequence = 0;
  ValueType type = ValueType::kValue;
};

// REQUIRES: seq <= kMaxSequenceNumber.
[[nodiscard]] std::uint64_t PackSequenceAndType(SequenceNumber seq, ValueType type);

void AppendInternalKey(std::string& dst, const ParsedInternalKey& key);

[[nodiscard]] std::string MakeInternalKey(std::string_view user_key, SequenceNumber seq,
                                          ValueType type);

// nullopt if `internal_key` is shorter than 8 bytes or has an unknown type.
[[nodiscard]] std::optional<ParsedInternalKey> ParseInternalKey(std::string_view internal_key);

// REQUIRES: internal_key.size() >= 8.
[[nodiscard]] std::string_view ExtractUserKey(std::string_view internal_key);

// ---------------------------------------------------------------------------
// Comparators
// ---------------------------------------------------------------------------
class Comparator {
 public:
  virtual ~Comparator() = default;
  // <0, 0, >0 like memcmp.
  [[nodiscard]] virtual int Compare(std::string_view a, std::string_view b) const = 0;
  [[nodiscard]] virtual std::string_view Name() const = 0;

  // Index-block key shortening (M4 optimization; a no-op is correct):
  // If start < limit, may change `start` to a shorter string in [start, limit).
  virtual void FindShortestSeparator(std::string& start, std::string_view limit) const = 0;
  // May change `key` to a shorter string >= key.
  virtual void FindShortSuccessor(std::string& key) const = 0;
};

// Lexicographic unsigned-byte order. Never destroyed.
const Comparator* BytewiseComparator();

class InternalKeyComparator final : public Comparator {
 public:
  explicit InternalKeyComparator(const Comparator* user_comparator = BytewiseComparator())
      : user_(user_comparator) {}

  [[nodiscard]] int Compare(std::string_view a, std::string_view b) const override;
  [[nodiscard]] std::string_view Name() const override { return "strata.InternalKeyComparator"; }
  void FindShortestSeparator(std::string& start, std::string_view limit) const override;
  void FindShortSuccessor(std::string& key) const override;

  [[nodiscard]] const Comparator* user_comparator() const { return user_; }

 private:
  const Comparator* user_;
};

// ---------------------------------------------------------------------------
// Point lookups
// ---------------------------------------------------------------------------

// The key to Seek() to when looking up `user_key` as of `snapshot`:
// internal_key(user_key, snapshot, kValueTypeForSeek).
class LookupKey {
 public:
  LookupKey(std::string_view user_key, SequenceNumber snapshot);

  [[nodiscard]] std::string_view internal_key() const;
  [[nodiscard]] std::string_view user_key() const;

 private:
  // TODO(M2): representation. (Small-buffer optimization is a nice M3 stretch:
  // most keys are short; avoid a heap allocation per Get.)
};

// Result of looking a key up in ONE layer (memtable or one SSTable):
//   KeyAbsent  - this layer has nothing for the key; keep searching older layers
//   KeyDeleted - newest visible entry is a tombstone; stop, the key is gone
//   string     - newest visible entry is this value; stop
struct KeyAbsent {};
struct KeyDeleted {};
using LookupResult = std::variant<KeyAbsent, KeyDeleted, std::string>;

}  // namespace strata
