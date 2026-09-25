#include "strata/status.h"

#include <cerrno>
#include <cstring>
#include <string>

namespace strata {

std::string_view StatusCodeName(StatusCode code) noexcept {
  switch (code) {
    case StatusCode::kOk: return "OK";
    case StatusCode::kNotFound: return "NotFound";
    case StatusCode::kCorruption: return "Corruption";
    case StatusCode::kInvalidArgument: return "InvalidArgument";
    case StatusCode::kIOError: return "IOError";
    case StatusCode::kNotSupported: return "NotSupported";
    case StatusCode::kBusy: return "Busy";
    case StatusCode::kAborted: return "Aborted";
    case StatusCode::kNotImplemented: return "NotImplemented";
  }
  return "Unknown";
}

Status::Status(StatusCode code, std::string_view msg, std::string_view detail) : code_(code) {
  msg_.reserve(msg.size() + (detail.empty() ? 0 : detail.size() + 2));
  msg_.append(msg);
  if (!detail.empty()) {
    msg_.append(": ");
    msg_.append(detail);
  }
}

Status Status::FromErrno(std::string_view context, int err) {
  const char* text = std::strerror(err);
  if (err == ENOENT) return NotFound(context, text);
  return IOError(context, text);
}

std::string Status::ToString() const {
  if (ok()) return "OK";
  std::string out(StatusCodeName(code_));
  if (!msg_.empty()) {
    out.append(": ");
    out.append(msg_);
  }
  return out;
}

}  // namespace strata
