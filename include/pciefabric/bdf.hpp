#pragma once
// Canonical PCI BDF (bus:device.function) identity plus domain/segment.
// A BDF identifies a device slot; it is NOT by itself authority for mutable
// runtime state (see ids.hpp generations).
#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

#include "pciefabric/ids.hpp"
#include "pciefabric/result.hpp"

namespace pci {

// Canonical BDF value. Parenthesized domain is preserved where parsed.
class Bdf {
public:
  constexpr Bdf() noexcept = default;
  constexpr Bdf(std::uint16_t domain, std::uint8_t bus,
                std::uint8_t device, std::uint8_t function) noexcept
      : domain_(domain), bus_(bus), device_(device), function_(function) {}

  [[nodiscard]] constexpr std::uint16_t domain() const noexcept { return domain_; }
  [[nodiscard]] constexpr std::uint8_t bus() const noexcept { return bus_; }
  [[nodiscard]] constexpr std::uint8_t device() const noexcept { return device_; }
  [[nodiscard]] constexpr std::uint8_t function() const noexcept { return function_; }

  // Function number must be in [0,7], device in [0,31], bus in [0,255].
  [[nodiscard]] constexpr bool valid() const noexcept {
    return function_ <= 7 && device_ <= 31;
  }

  // The domain is not part of the classic BDF triple; expose it separately.
  [[nodiscard]] constexpr bool has_domain() const noexcept { return domain_ != 0; }

  // Canonical "bus:device.function" (e.g. "01:00.0").
  [[nodiscard]] std::string bdf_string() const {
    char buf[12];
    const char* end = fmt_bdf(buf, sizeof(buf));
    return std::string(buf, static_cast<std::size_t>(end - buf));
  }

  // Canonical "domain:bus:device.function" (e.g. "0000:01:00.0").
  [[nodiscard]] std::string canonical() const {
    char buf[32];
    const char* end = fmt_canonical(buf, sizeof(buf));
    return std::string(buf, static_cast<std::size_t>(end - buf));
  }

  [[nodiscard]] bool operator==(const Bdf& o) const noexcept = default;
  [[nodiscard]] std::strong_ordering operator<=>(const Bdf& o) const noexcept {
    if (domain_ != o.domain_) return domain_ <=> o.domain_;
    if (bus_ != o.bus_) return bus_ <=> o.bus_;
    if (device_ != o.device_) return device_ <=> o.device_;
    return function_ <=> o.function_;
  }

  // Parse "dd:bb:ff.f" or "dddd:dd:bb:ff.f" (domain may be present or absent).
  static Result<Bdf> parse(std::string_view sv);

  // The strong identity produced from a canonical string. Zero/null if invalid.
  [[nodiscard]] static Result<PciBdf> to_identity(std::string_view sv);

  static constexpr std::uint8_t max_bus() noexcept { return 255; }
  static constexpr std::uint8_t max_device() noexcept { return 31; }
  static constexpr std::uint8_t max_function() noexcept { return 7; }

private:
  char* fmt_bdf(char* out, std::size_t n) const {
    char* p = out;
    std::uint8_t b = bus_, d = device_, f = function_;
    constexpr char hex[] = "0123456789abcdef";
    p[0] = hex[b >> 4]; p[1] = hex[b & 15]; p[2] = ':';
    p[3] = hex[d >> 4]; p[4] = hex[d & 15]; p[5] = '.';
    p[6] = hex[f & 15];
    (void)n;
    return p + 7;
  }
  char* fmt_canonical(char* out, std::size_t n) const {
    char* p = out; char* e = out + n;
    std::uint16_t dom = domain_;
    constexpr char hex[] = "0123456789abcdef";
    for (int i = 3; i >= 0; --i) { *p++ = hex[(dom >> (i * 4)) & 15]; }
    *p++ = ':';
    char* rest = fmt_bdf(p, static_cast<std::size_t>(e - p));
    return rest;
  }

  std::uint16_t domain_{};
  std::uint8_t bus_{};
  std::uint8_t device_{};
  std::uint8_t function_{};
};

[[nodiscard]] inline std::string to_string(const Bdf& b) { return b.canonical(); }

namespace detail {
[[nodiscard]] inline bool parse_hex_byte(std::string_view sv, std::size_t& pos,
                                          std::uint8_t& out, bool two_digits) {
  if (pos >= sv.size()) return false;
  auto hexval = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  if (two_digits) {
    if (pos + 2 > sv.size()) return false;
    int hi = hexval(sv[pos]); int lo = hexval(sv[pos + 1]);
    if (hi < 0 || lo < 0) return false;
    out = static_cast<std::uint8_t>((hi << 4) | lo);
    pos += 2;
    return true;
  }
  int lo = hexval(sv[pos]);
  if (lo < 0) return false;
  out = static_cast<std::uint8_t>(lo);
  pos += 1;
  return true;
}
[[nodiscard]] inline bool parse_hex_u16(std::string_view sv, std::size_t& pos, std::uint16_t& out) {
  if (pos + 4 > sv.size()) return false;
  auto hexval = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::uint16_t v = 0;
  for (int i = 0; i < 4; ++i) {
    int h = hexval(sv[pos + i]);
    if (h < 0) return false;
    v = static_cast<std::uint16_t>((v << 4) | static_cast<std::uint16_t>(h));
  }
  pos += 4;
  out = v;
  return true;
}
}  // namespace detail

inline Result<Bdf> Bdf::parse(std::string_view sv) {
  if (sv.empty()) return Result<Bdf>::err(ErrorCode::INVALID_BDF, "empty BDF");
  std::size_t pos = 0;
  std::uint16_t domain = 0;
  std::uint8_t bus = 0, device = 0, function = 0;

  // Count ':' separators to decide whether a domain is present.
  int colons = 0;
  for (char c : sv) if (c == ':') ++colons;

  if (colons == 1) {
    // bus:device.function
    if (!detail::parse_hex_byte(sv, pos, bus, true)) return Result<Bdf>::err(ErrorCode::INVALID_BDF, "bad bus");
    if (pos >= sv.size() || sv[pos] != ':') return Result<Bdf>::err(ErrorCode::INVALID_BDF, "missing ':'");
    ++pos;
    if (!detail::parse_hex_byte(sv, pos, device, true)) return Result<Bdf>::err(ErrorCode::INVALID_BDF, "bad device");
    if (pos >= sv.size() || sv[pos] != '.') return Result<Bdf>::err(ErrorCode::INVALID_BDF, "missing '.'");
    ++pos;
    if (!detail::parse_hex_byte(sv, pos, function, false)) return Result<Bdf>::err(ErrorCode::INVALID_BDF, "bad function");
  } else if (colons == 2) {
    // dddd:bus:device.function
    if (!detail::parse_hex_u16(sv, pos, domain)) return Result<Bdf>::err(ErrorCode::INVALID_BDF, "bad domain");
    if (pos >= sv.size() || sv[pos] != ':') return Result<Bdf>::err(ErrorCode::INVALID_BDF, "missing ':' after domain");
    ++pos;
    if (!detail::parse_hex_byte(sv, pos, bus, true)) return Result<Bdf>::err(ErrorCode::INVALID_BDF, "bad bus");
    if (pos >= sv.size() || sv[pos] != ':') return Result<Bdf>::err(ErrorCode::INVALID_BDF, "missing ':' after bus");
    ++pos;
    if (!detail::parse_hex_byte(sv, pos, device, true)) return Result<Bdf>::err(ErrorCode::INVALID_BDF, "bad device");
    if (pos >= sv.size() || sv[pos] != '.') return Result<Bdf>::err(ErrorCode::INVALID_BDF, "missing '.'");
    ++pos;
    if (!detail::parse_hex_byte(sv, pos, function, false)) return Result<Bdf>::err(ErrorCode::INVALID_BDF, "bad function");
  } else {
    return Result<Bdf>::err(ErrorCode::INVALID_BDF, "wrong number of ':' separators");
  }

  if (pos != sv.size()) return Result<Bdf>::err(ErrorCode::INVALID_BDF, "trailing characters");
  Bdf b(domain, bus, device, function);
  if (!b.valid()) return Result<Bdf>::err(ErrorCode::INVALID_BDF, "out-of-range bus/device/function");
  return Result<Bdf>::success(b);
}

inline Result<PciBdf> Bdf::to_identity(std::string_view sv) {
  auto parsed = parse(sv);
  if (!parsed) return Result<PciBdf>::err(parsed.code(), parsed.message());
  Bdf b = parsed.value();
  std::uint64_t v = (static_cast<std::uint64_t>(b.domain()) << 32) |
                    (static_cast<std::uint64_t>(b.bus()) << 24) |
                    (static_cast<std::uint64_t>(b.device()) << 8) |
                    static_cast<std::uint64_t>(b.function());
  return Result<PciBdf>::success(PciBdf(v));
}

}  // namespace pci

namespace std {
template <>
struct hash<::pci::Bdf> {
  std::size_t operator()(const ::pci::Bdf& b) const noexcept {
    std::uint64_t v = (static_cast<std::uint64_t>(b.domain()) << 32) |
                      (static_cast<std::uint64_t>(b.bus()) << 24) |
                      (static_cast<std::uint64_t>(b.device()) << 8) |
                      static_cast<std::uint64_t>(b.function());
    return std::hash<std::uint64_t>{}(v);
  }
};
}  // namespace std