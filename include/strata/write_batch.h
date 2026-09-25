#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include "strata/status.h"
#include "strata/todo.h"

namespace strata {

struct PutOp {
  std::string_view key;
  std::string_view value;
};

struct DeleteOp {
  std::string_view key;
};

using BatchOp = std::variant<PutOp, DeleteOp>;

// An ordered set of updates applied atomically by DB::Write(). A value type:
// copyable and movable (think about which special members you actually need
// to write; the answer should be "none").
//
// Wire format (docs/design.md §2; this is exactly the WAL record payload):
//
//   rep := sequence:fixed64 | count:fixed32 | op*
//   op  := 0x01 varstring(key) varstring(value)     // Put
//        | 0x00 varstring(key)                      // Delete
//   varstring := varint32(length) bytes
//
// `sequence` is the sequence number of the first op; op i gets sequence+i.
class WriteBatch {
 public:
  WriteBatch();

  void Put(std::string_view key, std::string_view value);
  void Delete(std::string_view key);
  void Clear();

  // Appends the ops of `other` after this batch's ops. Keeps this sequence.
  void Append(const WriteBatch& other);

  [[nodiscard]] std::uint32_t Count() const;
  [[nodiscard]] std::size_t ApproximateSize() const;

  [[nodiscard]] std::uint64_t Sequence() const;
  void SetSequence(std::uint64_t seq);

  // Calls fn(const BatchOp&) for each op, in insertion order. Returns
  // Corruption if the representation is malformed (e.g. a batch decoded from
  // a damaged WAL record). Views inside ops point into this batch.
  //
  //   batch.ForEach(Overloaded{
  //       [&](const PutOp& op) { ... },
  //       [&](const DeleteOp& op) { ... }});   // with std::visit inside fn
  template <class Fn>
    requires std::invocable<Fn&, const BatchOp&>
  Status ForEach(Fn&& fn) const {
    // TODO(M2, C++): type-erase `fn` into a (function pointer, void* context)
    // pair and call ForEachImpl. Questions to answer in your commit message:
    //   - Why not take std::function<void(const BatchOp&)>?
    //   - What is the lifetime of `fn` relative to the context pointer?
    //   - Why is ForEach a template but ForEachImpl not?
    static_cast<void>(fn);
    STRATA_TODO("M2", "WriteBatch::ForEach type erasure");
  }

  // The serialized representation; valid until the batch is modified.
  [[nodiscard]] std::string_view Contents() const { return rep_; }

  // Adopts a serialized representation. Returns Corruption if it is too small
  // to hold the header. (Op-level validation happens in ForEach.)
  static Result<WriteBatch> FromContents(std::string_view contents);

 private:
  using OpThunk = void (*)(void* ctx, const BatchOp& op);
  Status ForEachImpl(OpThunk thunk, void* ctx) const;

  std::string rep_;
};

// The "overloaded" idiom for std::visit. Provided so tests can use it.
template <class... Fs>
struct Overloaded : Fs... {
  using Fs::operator()...;
};

}  // namespace strata
