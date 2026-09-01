#pragma once
// Explicit error/result type. PCIe Fabric does not rely on exceptions for
// routine control flow; it propagates typed errors.
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace pci {

enum class ErrorCode : int {
  OK = 0,
  INVALID_ARGUMENT = 1,
  INVALID_BDF = 2,
  INVALID_ENUM = 3,
  INVALID_GENERATION = 4,
  INVALID_VALUE = 5,
  DUPLICATE_IDENTITY = 6,
  DUPLICATE_CURRENT = 7,
  NOT_FOUND = 8,
  ALREADY_EXISTS = 9,
  STALE_AUTHORITY = 10,
  STALE_EPOCH = 11,
  STALE_BOOT = 12,
  GENERATION_ROLLBACK = 13,
  HIERARCHY_CYCLE = 14,
  INVALID_RELATIONSHIP = 15,
  OVERCOMMIT = 16,
  DOUBLE_RELEASE = 17,
  DUPLICATE_RELEASE = 18,
  ACCOUNTING_DRIFT = 19,
  PATH_INVALID = 20,
  REVALIDATION_REQUIRED = 21,
  UNKNOWN_CAPABILITY = 22,
  PROTOCOL_MALFORMED = 23,
  PROTOCOL_CHECKSUM = 24,
  PROTOCOL_VERSION = 25,
  PROTOCOL_OVERFLOW = 26,
  TRUNCATED = 27,
  IO_ERROR = 28,
  NETWORK_ERROR = 29,
  PEER_LOST = 30,
  TIMEOUT = 31,
  NOT_IMPLEMENTED = 32,
  CUDA_ERROR = 33,
  WINDOWS_ERROR = 34,
  SERIALIZATION_ERROR = 35,
  INTERNAL = 36,
  CONFLICT = 37,
  LIMIT_EXCEEDED = 38
};

[[nodiscard]] inline const char* to_string(ErrorCode c) noexcept {
  switch (c) {
    case ErrorCode::OK: return "OK";
    case ErrorCode::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case ErrorCode::INVALID_BDF: return "INVALID_BDF";
    case ErrorCode::INVALID_ENUM: return "INVALID_ENUM";
    case ErrorCode::INVALID_GENERATION: return "INVALID_GENERATION";
    case ErrorCode::INVALID_VALUE: return "INVALID_VALUE";
    case ErrorCode::DUPLICATE_IDENTITY: return "DUPLICATE_IDENTITY";
    case ErrorCode::DUPLICATE_CURRENT: return "DUPLICATE_CURRENT";
    case ErrorCode::NOT_FOUND: return "NOT_FOUND";
    case ErrorCode::ALREADY_EXISTS: return "ALREADY_EXISTS";
    case ErrorCode::STALE_AUTHORITY: return "STALE_AUTHORITY";
    case ErrorCode::STALE_EPOCH: return "STALE_EPOCH";
    case ErrorCode::STALE_BOOT: return "STALE_BOOT";
    case ErrorCode::GENERATION_ROLLBACK: return "GENERATION_ROLLBACK";
    case ErrorCode::HIERARCHY_CYCLE: return "HIERARCHY_CYCLE";
    case ErrorCode::INVALID_RELATIONSHIP: return "INVALID_RELATIONSHIP";
    case ErrorCode::OVERCOMMIT: return "OVERCOMMIT";
    case ErrorCode::DOUBLE_RELEASE: return "DOUBLE_RELEASE";
    case ErrorCode::DUPLICATE_RELEASE: return "DUPLICATE_RELEASE";
    case ErrorCode::ACCOUNTING_DRIFT: return "ACCOUNTING_DRIFT";
    case ErrorCode::PATH_INVALID: return "PATH_INVALID";
    case ErrorCode::REVALIDATION_REQUIRED: return "REVALIDATION_REQUIRED";
    case ErrorCode::UNKNOWN_CAPABILITY: return "UNKNOWN_CAPABILITY";
    case ErrorCode::PROTOCOL_MALFORMED: return "PROTOCOL_MALFORMED";
    case ErrorCode::PROTOCOL_CHECKSUM: return "PROTOCOL_CHECKSUM";
    case ErrorCode::PROTOCOL_VERSION: return "PROTOCOL_VERSION";
    case ErrorCode::PROTOCOL_OVERFLOW: return "PROTOCOL_OVERFLOW";
    case ErrorCode::TRUNCATED: return "TRUNCATED";
    case ErrorCode::IO_ERROR: return "IO_ERROR";
    case ErrorCode::NETWORK_ERROR: return "NETWORK_ERROR";
    case ErrorCode::PEER_LOST: return "PEER_LOST";
    case ErrorCode::TIMEOUT: return "TIMEOUT";
    case ErrorCode::NOT_IMPLEMENTED: return "NOT_IMPLEMENTED";
    case ErrorCode::CUDA_ERROR: return "CUDA_ERROR";
    case ErrorCode::WINDOWS_ERROR: return "WINDOWS_ERROR";
    case ErrorCode::SERIALIZATION_ERROR: return "SERIALIZATION_ERROR";
    case ErrorCode::INTERNAL: return "INTERNAL";
    case ErrorCode::CONFLICT: return "CONFLICT";
    case ErrorCode::LIMIT_EXCEEDED: return "LIMIT_EXCEEDED";
  }
  return "INTERNAL";
}

// Result<T>: holds either a value or a typed error.
template <typename T>
class Result {
public:
  Result() noexcept : has_value_(false), code_(ErrorCode::INTERNAL) {}
  explicit Result(const T& v) : has_value_(true), code_(ErrorCode::OK) { new (&storage_) T(v); }
  explicit Result(T&& v) noexcept : has_value_(true), code_(ErrorCode::OK) { new (&storage_) T(std::move(v)); }
  Result(ErrorCode code, std::string msg) : has_value_(false), code_(code), message_(std::move(msg)) {}
  static Result success(T v) { return Result(std::move(v)); }
  static Result err(ErrorCode c, std::string m = {}) { return Result(c, std::move(m)); }

  Result(const Result& o) : has_value_(o.has_value_), code_(o.code_), message_(o.message_) {
    if (o.has_value_) new (&storage_) T(o.value());
  }
  Result& operator=(const Result& o) {
    if (this != &o) { reset(); has_value_ = o.has_value_; code_ = o.code_; message_ = o.message_; if (o.has_value_) new (&storage_) T(o.value()); }
    return *this;
  }
  Result(Result&& o) noexcept : has_value_(o.has_value_), code_(o.code_), message_(std::move(o.message_)) {
    if (o.has_value_) new (&storage_) T(std::move(o.value()));
  }
  Result& operator=(Result&& o) noexcept {
    if (this != &o) { reset(); has_value_ = o.has_value_; code_ = o.code_; message_ = std::move(o.message_); if (o.has_value_) new (&storage_) T(std::move(o.value())); }
    return *this;
  }
  ~Result() { reset(); }

  [[nodiscard]] bool ok() const noexcept { return has_value_ && code_ == ErrorCode::OK; }
  [[nodiscard]] bool failed() const noexcept { return !ok(); }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
  [[nodiscard]] const T& value() const { if (!has_value_) throw std::runtime_error("Result::value on error"); return value_ref(); }
  [[nodiscard]] T& value() { if (!has_value_) throw std::runtime_error("Result::value on error"); return value_ref(); }
  [[nodiscard]] const T& operator*() const { return value(); }
  [[nodiscard]] T& operator*() { return value(); }
  [[nodiscard]] const T* operator->() const { if (!has_value_) throw std::runtime_error("Result::operator-> on error"); return &value_ref(); }
  [[nodiscard]] T* operator->() { if (!has_value_) throw std::runtime_error("Result::operator-> on error"); return &value_ref(); }
  [[nodiscard]] ErrorCode code() const noexcept { return has_value_ ? ErrorCode::OK : code_; }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }
  [[nodiscard]] const T& value_or(const T& fallback) const { return ok() ? value_ref() : fallback; }

  // Monadic-ish: transform success value.
  template <typename F>
  auto map(F&& f) -> Result<decltype(f(value()))> {
    using R = decltype(f(value()));
    if (!ok()) return Result<R>::err(code(), message());
    return Result<R>::success(f(value_ref()));
  }

private:
  void reset() noexcept {
    if (has_value_) { value_ref().~T(); }
  }
  const T& value_ref() const { return *reinterpret_cast<const T*>(&storage_); }
  T& value_ref() { return *reinterpret_cast<T*>(&storage_); }

  alignas(T) unsigned char storage_[sizeof(T)];
  bool has_value_;
  ErrorCode code_;
  std::string message_;
};

// A type-erased result alias for operations that only signal success/failure.
template <>
class Result<void> {
public:
  Result() noexcept : code_(ErrorCode::OK) {}
  Result(ErrorCode code, std::string msg) : code_(code), message_(std::move(msg)) {}
  static Result success() { return Result(); }
  static Result err(ErrorCode c, std::string m = {}) { return Result(c, std::move(m)); }
  [[nodiscard]] bool ok() const noexcept { return code_ == ErrorCode::OK; }
  [[nodiscard]] bool failed() const noexcept { return !ok(); }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
  [[nodiscard]] ErrorCode code() const noexcept { return code_; }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }
private:
  ErrorCode code_;
  std::string message_;
};

}  // namespace pci
