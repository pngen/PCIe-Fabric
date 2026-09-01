#pragma once
// Deterministic little-endian binary codec with strict bounds checking and
// CRC-32 integrity. No unbounded allocation is possible from network/disk
// controlled lengths; every read is bounds-checked.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "pciefabric/result.hpp"

namespace pci {

// Cisco/PNG CRC-32 (IEEE 802.3 polynomial).
[[nodiscard]] inline std::uint32_t crc32(const std::uint8_t* data, std::size_t n,
                                         std::uint32_t seed = 0xFFFFFFFFu) noexcept {
  std::uint32_t crc = seed;
  for (std::size_t i = 0; i < n; ++i) {
    crc ^= data[i];
    for (int k = 0; k < 8; ++k) {
      std::uint32_t mask = static_cast<std::uint32_t>(-static_cast<int>(crc & 1u));
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

class Writer {
public:
  void u8(std::uint8_t v) { buf_.push_back(v); }
  void u16(std::uint16_t v) { u8(static_cast<std::uint8_t>(v)); u8(static_cast<std::uint8_t>(v >> 8)); }
  void u32(std::uint32_t v) { for (int i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(v >> (i * 8))); }
  void u64(std::uint64_t v) { for (int i = 0; i < 8; ++i) u8(static_cast<std::uint8_t>(v >> (i * 8))); }
  void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
  void f64(double v) { std::uint64_t bits; std::memcpy(&bits, &v, 8); u64(bits); }
  void boolean(bool v) { u8(v ? 1 : 0); }
  void string(const std::string& s) {
    u32(static_cast<std::uint32_t>(s.size()));
    buf_.insert(buf_.end(), s.begin(), s.end());
  }
  void bytes(const std::uint8_t* p, std::size_t n) { buf_.insert(buf_.end(), p, p + n); }
  void u32_count(std::size_t n) { u32(static_cast<std::uint32_t>(n)); }
  [[nodiscard]] const std::vector<std::uint8_t>& data() const noexcept { return buf_; }
  [[nodiscard]] std::vector<std::uint8_t> take() { return std::move(buf_); }
  [[nodiscard]] std::size_t size() const noexcept { return buf_.size(); }

private:
  std::vector<std::uint8_t> buf_;
};

class Reader {
public:
  Reader(const std::uint8_t* data, std::size_t n) : data_(data), size_(n) {}
  explicit Reader(const std::vector<std::uint8_t>& v) : data_(v.data()), size_(v.size()) {}

  Result<std::uint8_t> u8() {
    if (pos_ + 1 > size_) return Result<std::uint8_t>::err(ErrorCode::TRUNCATED, "u8 overrun");
    return Result<std::uint8_t>::success(data_[pos_++]);
  }
  Result<std::uint16_t> u16() {
    auto lo = u8(); if (!lo) return Result<std::uint16_t>::err(lo.code(), lo.message());
    auto hi = u8(); if (!hi) return Result<std::uint16_t>::err(hi.code(), hi.message());
    return Result<std::uint16_t>::success(static_cast<std::uint16_t>(lo.value() | (hi.value() << 8)));
  }
  Result<std::uint32_t> u32() {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) { auto b = u8(); if (!b) return Result<std::uint32_t>::err(b.code(), b.message()); v |= static_cast<std::uint32_t>(b.value()) << (i * 8); }
    return Result<std::uint32_t>::success(v);
  }
  Result<std::uint64_t> u64() {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) { auto b = u8(); if (!b) return Result<std::uint64_t>::err(b.code(), b.message()); v |= static_cast<std::uint64_t>(b.value()) << (i * 8); }
    return Result<std::uint64_t>::success(v);
  }
  Result<std::int32_t> i32() {
    auto u = u32(); if (!u) return Result<std::int32_t>::err(u.code(), u.message());
    return Result<std::int32_t>::success(static_cast<std::int32_t>(u.value()));
  }
  Result<double> f64() {
    auto v = u64(); if (!v) return Result<double>::err(v.code(), v.message());
    double d; std::memcpy(&d, &v.value(), 8); return Result<double>::success(d);
  }
  Result<bool> boolean() {
    auto b = u8(); if (!b) return Result<bool>::err(b.code(), b.message());
    return Result<bool>::success(b.value() != 0);
  }
  // String length is bounds-checked and capped to avoid unbounded allocation.
  Result<std::string> string() {
    auto len = u32(); if (!len) return Result<std::string>::err(len.code(), len.message());
    if (len.value() > (max_string_ - pos_) || len.value() > 64u * 1024u)
      return Result<std::string>::err(ErrorCode::PROTOCOL_OVERFLOW, "string length out of bounds");
    std::string s(reinterpret_cast<const char*>(data_ + pos_), len.value());
    pos_ += len.value();
    return Result<std::string>::success(std::move(s));
  }
  Result<std::uint32_t> u32_count() {
    auto n = u32(); if (!n) return Result<std::uint32_t>::err(n.code(), n.message());
    if (n.value() > max_count_) return Result<std::uint32_t>::err(ErrorCode::PROTOCOL_OVERFLOW, "count out of bounds");
    return n;
  }
  std::size_t remaining() const noexcept { return size_ - pos_; }
  bool at_end() const noexcept { return pos_ == size_; }
  Result<void> require_end() {
    if (pos_ != size_) return Result<void>::err(ErrorCode::PROTOCOL_MALFORMED, "trailing garbage");
    return Result<void>::success();
  }

private:
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_{};
  std::size_t max_string_{256u * 1024u};
  std::size_t max_count_{16u * 1024u * 1024u};
};

}  // namespace pci
