#pragma once
// Provenance is preserved through every value that carries an observation or a
// derived quantity. It distinguishes measured, reported, derived, estimated,
// synthetic, and unknown origins.
#include <cstdint>
#include <string>

#include "pciefabric/enums.hpp"
#include "pciefabric/values.hpp"

namespace pci {

// A scalar together with explicit provenance, confidence and (optional)
// freshness. This is the universal carrier for facts.
template <typename T>
class Provenanced {
public:
  Provenanced() = default;
  Provenanced(T value, Provenance p, Confidence c = Confidence(1.0))
      : value_(std::move(value)), provenance_(p), confidence_(c) {}

  [[nodiscard]] const T& value() const noexcept { return value_; }
  [[nodiscard]] T& value() noexcept { return value_; }
  [[nodiscard]] Provenance provenance() const noexcept { return provenance_; }
  [[nodiscard]] Confidence confidence() const noexcept { return confidence_; }

  [[nodiscard]] bool measured() const noexcept { return provenance_ == Provenance::MEASURED; }
  [[nodiscard]] bool derived() const noexcept { return provenance_ == Provenance::DERIVED; }
  [[nodiscard]] bool synthetic() const noexcept { return provenance_ == Provenance::SYNTHETIC; }
  [[nodiscard]] bool unknown() const noexcept { return provenance_ == Provenance::UNKNOWN; }

  void set_provenance(Provenance p) noexcept { provenance_ = p; }

private:
  T value_{};
  Provenance provenance_{Provenance::UNKNOWN};
  Confidence confidence_{Confidence(1.0)};
};

// Freshness state record. A persisted value is never implicitly current.
class FreshnessRecord {
public:
  FreshnessRecord() = default;
  FreshnessRecord(Freshness s, std::uint64_t age_ms = 0) : state_(s), age_ms_(age_ms) {}

  [[nodiscard]] Freshness state() const noexcept { return state_; }
  [[nodiscard]] std::uint64_t age_ms() const noexcept { return age_ms_; }

  [[nodiscard]] bool is_current() const noexcept { return state_ == Freshness::CURRENT; }
  [[nodiscard]] bool requires_revalidation() const noexcept {
    return state_ == Freshness::REVALIDATION_REQUIRED || state_ == Freshness::INVALID;
  }

  void set_state(Freshness s) noexcept { state_ = s; }
  void set_age_ms(std::uint64_t ms) noexcept { age_ms_ = ms; }

private:
  Freshness state_{Freshness::REVALIDATION_REQUIRED};
  std::uint64_t age_ms_{};
};

// Common timestamps are epoch milliseconds; durable timestamps are typed.
using EpochMs = Milliseconds;

}  // namespace pci