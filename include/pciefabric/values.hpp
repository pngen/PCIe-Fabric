#pragma once
// Typed physical quantities. Every quantity is a distinct type with a validity
// predicate that rejects NaN, infinity, and impossible values. Quantities are
// never implicitly convertible to raw arithmetic values.
#include <cmath>
#include <cstdint>
#include <string>

#include "pciefabric/result.hpp"

namespace pci {

namespace detail {
[[nodiscard]] inline std::string bad_value(const char* name, std::string reason) {
  return std::string(name) + ": " + reason;
}
}  // namespace detail

// ---------------------------------------------------------------------------
// Integer-backed quantities (bytes, counts, durations, lanes).
// ---------------------------------------------------------------------------
#define PCIEFABRIC_INT_QUANTITY(NAME, REP)                                        class NAME {                                                                   public:                                                                          using rep_type = REP;                                                          constexpr NAME() noexcept = default;                                           explicit constexpr NAME(REP v) noexcept : v_(v) {}                             [[nodiscard]] static constexpr bool is_valid(REP v) noexcept { return v >= 0; }     [[nodiscard]] constexpr REP value() const noexcept { return v_; }              [[nodiscard]] constexpr bool zero() const noexcept { return v_ == 0; }         [[nodiscard]] constexpr bool empty() const noexcept { return v_ == 0; }        static Result<NAME> checked(REP v) {                                             if (!is_valid(v)) return Result<NAME>::err(ErrorCode::INVALID_VALUE,               detail::bad_value(#NAME, "negative value"));                              return Result<NAME>::success(NAME(v));                                            }                                                                              friend constexpr bool operator==(NAME, NAME) noexcept = default;               friend constexpr bool operator<(NAME a, NAME b) noexcept { return a.v_ < b.v_; }     friend constexpr bool operator>(NAME a, NAME b) noexcept { return a.v_ > b.v_; }     friend constexpr bool operator<=(NAME a, NAME b) noexcept { return a.v_ <= b.v_; }     friend constexpr bool operator>=(NAME a, NAME b) noexcept { return a.v_ >= b.v_; }     constexpr NAME operator+(NAME o) const { return NAME(static_cast<REP>(v_ + o.v_)); }      constexpr NAME operator-(NAME o) const { return NAME(static_cast<REP>(v_ - o.v_)); }    private:                                                                         REP v_{};                                                                    };

// Byte and size quantities.
PCIEFABRIC_INT_QUANTITY(Bytes, std::uint64_t)
PCIEFABRIC_INT_QUANTITY(PayloadBytes, std::uint64_t)
PCIEFABRIC_INT_QUANTITY(TransferBytes, std::uint64_t)
PCIEFABRIC_INT_QUANTITY(CapacityBytes, std::uint64_t)
PCIEFABRIC_INT_QUANTITY(Nanoseconds, std::uint64_t)
PCIEFABRIC_INT_QUANTITY(Microseconds, std::uint64_t)
PCIEFABRIC_INT_QUANTITY(Milliseconds, std::uint64_t)
PCIEFABRIC_INT_QUANTITY(LaneCount, std::uint16_t)
PCIEFABRIC_INT_QUANTITY(QueueDepth, std::uint32_t)
PCIEFABRIC_INT_QUANTITY(OutstandingOps, std::uint32_t)
PCIEFABRIC_INT_QUANTITY(RetryCount, std::uint64_t)
PCIEFABRIC_INT_QUANTITY(ErrorCount, std::uint64_t)
PCIEFABRIC_INT_QUANTITY(OpCount, std::uint64_t)
PCIEFABRIC_INT_QUANTITY(IterationCount, std::uint64_t)

#undef PCIEFABRIC_INT_QUANTITY

// ---------------------------------------------------------------------------
// Double-backed quantities (rates, scores, ratios). Fractional types bound to
// [0,1]; others must be finite and non-negative.
// ---------------------------------------------------------------------------
#define PCIEFABRIC_DOUBLE_QUANTITY(NAME, PRED, XML)                              class NAME {                                                                   public:                                                                          using rep_type = double;                                                       constexpr NAME() noexcept = default;                                           explicit constexpr NAME(double v) noexcept : v_(v) {}                          [[nodiscard]] static bool is_valid(double v) noexcept {                return std::isfinite(v) && (PRED);                                           }                                                                              [[nodiscard]] constexpr double value() const noexcept { return v_; }           static Result<NAME> checked(double v) {                                          if (!is_valid(v)) return Result<NAME>::err(ErrorCode::INVALID_VALUE,               detail::bad_value(#NAME, XML));                                            return Result<NAME>::success(NAME(v));                                            }                                                                              friend constexpr bool operator==(NAME, NAME) noexcept = default;               friend constexpr bool operator<(NAME a, NAME b) noexcept { return a.v_ < b.v_; }     friend constexpr bool operator>(NAME a, NAME b) noexcept { return a.v_ > b.v_; }   private:                                                                         double v_{};                                                                 };

PCIEFABRIC_DOUBLE_QUANTITY(BytesPerSecond, v >= 0.0, "negative or non-finite bytes/sec")
PCIEFABRIC_DOUBLE_QUANTITY(GiBPerSecond, v >= 0.0, "negative or non-finite GiB/sec")
PCIEFABRIC_DOUBLE_QUANTITY(Gtps, v >= 0.0, "negative or non-finite GT/s")
PCIEFABRIC_DOUBLE_QUANTITY(Utilization, v >= 0.0 && v <= 1.0, "utilization outside [0,1]")
PCIEFABRIC_DOUBLE_QUANTITY(TemperatureC, (v >= -50.0 && v <= 200.0), "temperature outside [-50,200] C")
PCIEFABRIC_DOUBLE_QUANTITY(PathCost, v >= 0.0, "negative path cost")
PCIEFABRIC_DOUBLE_QUANTITY(ContentionScore, v >= 0.0 && v <= 1.0, "contention outside [0,1]")
PCIEFABRIC_DOUBLE_QUANTITY(Confidence, v >= 0.0 && v <= 1.0, "confidence outside [0,1]")
PCIEFABRIC_DOUBLE_QUANTITY(Percent, v >= 0.0 && v <= 1.0, "percentage outside [0,1]")

#undef PCIEFABRIC_DOUBLE_QUANTITY

// ---------------------------------------------------------------------------
// Common conversions.
// ---------------------------------------------------------------------------
[[nodiscard]] constexpr double to_gib_per_sec(BytesPerSecond bps) {
  return bps.value() / (1024.0 * 1024.0 * 1024.0);
}
[[nodiscard]] constexpr double to_mib_per_sec(BytesPerSecond bps) {
  return bps.value() / (1024.0 * 1024.0);
}
[[nodiscard]] constexpr BytesPerSecond from_mib_per_sec(double mib) {
  return BytesPerSecond(mib * 1024.0 * 1024.0);
}

}  // namespace pci
