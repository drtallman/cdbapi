#pragma once

#include <string>
#include <string_view>

namespace cdbapi {

enum class ErrorCode {
  kOk = 0,
  kInvalidArgument,
  kInvalidPath,
  kInvalidCrs,
  kInvalidLod,
  kInvalidGeoCell,
  kIoError,
  kGdalError,
  kMetadataError,
  kAlreadyExists,
  kNotFound,
};

class Error {
 public:
  Error(ErrorCode code, std::string message)
      : code_(code), message_(std::move(message)) {}

  static auto Ok() -> Error { return Error(ErrorCode::kOk, ""); }

  auto code() const -> ErrorCode { return code_; }
  auto message() const -> std::string_view { return message_; }

  explicit operator bool() const { return code_ != ErrorCode::kOk; }

 private:
  ErrorCode code_;
  std::string message_;
};

}  // namespace cdbapi
