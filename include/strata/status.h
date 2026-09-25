#pragma once

// Status and Result<T>: strata's error model.
//
// Rules of the road (graded in reviews, see docs/cpp-rubric.md):
//   * Every fallible operation returns Status or Result<T>. Exceptions never
//     cross the engine's API boundary (the only exception in the codebase is
//     the scaffold's NotImplementedError, which disappears as you finish).
//   * Status is [[nodiscard]]: ignoring an error is a compile warning. If you
//     truly mean to ignore one, say so explicitly: `static_cast<void>(s);`
//     plus a comment explaining why that is safe.
//   * Result<T> = std::expected<T, Status>. An error Result never holds OK.

#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace strata {

enum class StatusCode : unsigned char {
  kOk = 0,
  kNotFound,
  kCorruption,
  kInvalidArgument,
  kIOError,
  kNotSupported,
  kBusy,
  kAborted,
  kNotImplemented,
};

std::string_view StatusCodeName(StatusCode code) noexcept;

class [[nodiscard]] Status {
 public:
  Status() noexcept = default;  // OK

  static Status OK() noexcept { return {}; }
  static Status NotFound(std::string_view msg, std::string_view detail = {}) {
    return {StatusCode::kNotFound, msg, detail};
  }
  static Status Corruption(std::string_view msg, std::string_view detail = {}) {
    return {StatusCode::kCorruption, msg, detail};
  }
  static Status InvalidArgument(std::string_view msg, std::string_view detail = {}) {
    return {StatusCode::kInvalidArgument, msg, detail};
  }
  static Status IOError(std::string_view msg, std::string_view detail = {}) {
    return {StatusCode::kIOError, msg, detail};
  }
  static Status NotSupported(std::string_view msg, std::string_view detail = {}) {
    return {StatusCode::kNotSupported, msg, detail};
  }
  static Status Busy(std::string_view msg, std::string_view detail = {}) {
    return {StatusCode::kBusy, msg, detail};
  }
  static Status Aborted(std::string_view msg, std::string_view detail = {}) {
    return {StatusCode::kAborted, msg, detail};
  }
  static Status NotImplemented(std::string_view msg, std::string_view detail = {}) {
    return {StatusCode::kNotImplemented, msg, detail};
  }

  // Builds an IOError from an errno value: "<context>: <strerror(err)>".
  static Status FromErrno(std::string_view context, int err);

  [[nodiscard]] bool ok() const noexcept { return code_ == StatusCode::kOk; }
  [[nodiscard]] StatusCode code() const noexcept { return code_; }
  [[nodiscard]] const std::string& message() const noexcept { return msg_; }

  [[nodiscard]] bool IsNotFound() const noexcept { return code_ == StatusCode::kNotFound; }
  [[nodiscard]] bool IsCorruption() const noexcept { return code_ == StatusCode::kCorruption; }
  [[nodiscard]] bool IsIOError() const noexcept { return code_ == StatusCode::kIOError; }
  [[nodiscard]] bool IsInvalidArgument() const noexcept {
    return code_ == StatusCode::kInvalidArgument;
  }
  [[nodiscard]] bool IsBusy() const noexcept { return code_ == StatusCode::kBusy; }
  [[nodiscard]] bool IsNotImplemented() const noexcept {
    return code_ == StatusCode::kNotImplemented;
  }

  // "OK" or "<CodeName>: <message>".
  [[nodiscard]] std::string ToString() const;

 private:
  Status(StatusCode code, std::string_view msg, std::string_view detail);

  StatusCode code_ = StatusCode::kOk;
  std::string msg_;
};

template <class T>
using Result = std::expected<T, Status>;

// Convenience for returning an error from a function returning Result<T>:
//   return Err(Status::Corruption("bad footer"));
inline std::unexpected<Status> Err(Status s) { return std::unexpected<Status>(std::move(s)); }

}  // namespace strata

// Early-return for functions that return Status.
#define STRATA_RETURN_IF_ERROR(expr)                 \
  do {                                               \
    ::strata::Status strata_status_ = (expr);        \
    if (!strata_status_.ok()) return strata_status_; \
  } while (0)

// TODO(M1, C++): STRATA_RETURN_IF_ERROR only works in functions returning
// Status. You will write a *lot* of code shaped like:
//
//   auto file = fs.NewWritableFile(path);
//   if (!file) return Err(file.error());
//   auto& f = *file;
//
// Design a helper that removes this boilerplate for Result<T>, e.g.
//   STRATA_ASSIGN_OR_RETURN(auto f, fs.NewWritableFile(path));
// that works whether the enclosing function returns Status or Result<U>.
// Things to get right: evaluating `expr` exactly once, moving (not copying)
// the value out, unique temporary names (__LINE__ / __COUNTER__ pasting),
// and making the early-return type-correct for both Status and Result<U>.
// (Hint: a small proxy type with conversion operators to Status and to
// std::unexpected<Status> solves the "both return types" problem.)
