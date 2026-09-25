#include "util/coding.h"

#include "strata/todo.h"

namespace strata {

// TODO(M1): Implement every function below. Constraints:
//   - Output must be byte-identical on little- and big-endian hosts. You
//     can't test big-endian on an M-series Mac or aarch64 Lima, so reason it
//     through and make the code obviously correct (the tests pin exact bytes).
//   - No heap allocation beyond what std::string::append itself does.
//   - Put* functions should do ONE append per call (encode into a small stack
//     buffer first). Why does that matter for a WAL writer's hot path?

void EncodeFixed32(char* /*dst*/, std::uint32_t /*value*/) { STRATA_TODO("M1", "EncodeFixed32"); }

void EncodeFixed64(char* /*dst*/, std::uint64_t /*value*/) { STRATA_TODO("M1", "EncodeFixed64"); }

std::uint32_t DecodeFixed32(const char* /*src*/) { STRATA_TODO("M1", "DecodeFixed32"); }

std::uint64_t DecodeFixed64(const char* /*src*/) { STRATA_TODO("M1", "DecodeFixed64"); }

void PutFixed32(std::string& /*dst*/, std::uint32_t /*value*/) { STRATA_TODO("M1", "PutFixed32"); }

void PutFixed64(std::string& /*dst*/, std::uint64_t /*value*/) { STRATA_TODO("M1", "PutFixed64"); }

void PutVarint32(std::string& /*dst*/, std::uint32_t /*value*/) {
  STRATA_TODO("M1", "PutVarint32");
}

void PutVarint64(std::string& /*dst*/, std::uint64_t /*value*/) {
  STRATA_TODO("M1", "PutVarint64");
}

void PutLengthPrefixed(std::string& /*dst*/, std::string_view /*value*/) {
  STRATA_TODO("M1", "PutLengthPrefixed");
}

int VarintLength(std::uint64_t /*value*/) { STRATA_TODO("M1", "VarintLength"); }

char* EncodeVarint32(char* /*dst*/, std::uint32_t /*value*/) {
  STRATA_TODO("M1", "EncodeVarint32");
}

char* EncodeVarint64(char* /*dst*/, std::uint64_t /*value*/) {
  STRATA_TODO("M1", "EncodeVarint64");
}

std::optional<std::uint32_t> GetVarint32(std::string_view& /*input*/) {
  STRATA_TODO("M1", "GetVarint32");
}

std::optional<std::uint64_t> GetVarint64(std::string_view& /*input*/) {
  STRATA_TODO("M1", "GetVarint64");
}

std::optional<std::uint32_t> GetFixed32(std::string_view& /*input*/) {
  STRATA_TODO("M1", "GetFixed32");
}

std::optional<std::uint64_t> GetFixed64(std::string_view& /*input*/) {
  STRATA_TODO("M1", "GetFixed64");
}

std::optional<std::string_view> GetLengthPrefixed(std::string_view& /*input*/) {
  STRATA_TODO("M1", "GetLengthPrefixed");
}

}  // namespace strata
