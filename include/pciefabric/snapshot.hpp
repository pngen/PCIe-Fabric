#pragma once
// Immutable snapshots, deterministic diffs, and a stable semantic digest.
// The digest excludes incidental absolute values (memory addresses, unstable
// enumeration order) so that semantically identical replays digest identically.
#include <cstdint>
#include <string>
#include <vector>

#include "pciefabric/ids.hpp"
#include "pciefabric/measurement.hpp"
#include "pciefabric/path.hpp"
#include "pciefabric/reservation.hpp"
#include "pciefabric/topology.hpp"

namespace pci {

struct Snapshot {
  std::uint64_t sequence{};
  HostGeneration host_generation{};
  CoordinatorEpoch epoch{};
  std::vector<Node> nodes;            // deterministic order (by canonical BDF via all_nodes)
  std::vector<Path> paths;
  std::vector<Measurement> measurements;
  std::vector<Reservation> reservations;
  std::vector<PolicyId> policies;
  std::uint64_t semantic_digest{};
};

// Immutable diff between two snapshots.
struct SnapshotDiff {
  std::vector<Bdf> endpoints_appeared;
  std::vector<Bdf> endpoints_disappeared;
  std::vector<PciNodeId> generation_changed;
  std::vector<PathId> path_changed;
  std::vector<PathId> path_invalidated;
  std::vector<LinkId> link_capability_changed;
  std::vector<MeasurementId> measurement_changed;
  std::vector<ReservationId> reservation_changed;
  std::vector<PolicyId> policy_changed;
  bool authority_rolled{false};
  [[nodiscard]] bool empty() const noexcept {
    return endpoints_appeared.empty() && endpoints_disappeared.empty() &&
           generation_changed.empty() && path_changed.empty() && path_invalidated.empty() &&
           link_capability_changed.empty() && measurement_changed.empty() &&
           reservation_changed.empty() && policy_changed.empty() && !authority_rolled;
  }
};

// FNV-1a 64-bit hash over a deterministic byte stream.
[[nodiscard]] inline std::uint64_t fnv1a_64(std::uint64_t h, const void* p, std::size_t n) noexcept {
  const auto* b = static_cast<const std::uint8_t*>(p);
  for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}
[[nodiscard]] inline std::uint64_t fnv1a_64_str(std::uint64_t h, const std::string& s) noexcept {
  return fnv1a_64(h, s.data(), s.size());
}
[[nodiscard]] inline std::uint64_t fnv1a_64_u64(std::uint64_t h, std::uint64_t v) noexcept {
  return fnv1a_64(h, &v, sizeof(v));
}
[[nodiscard]] inline std::uint64_t fnv1a_64_u32(std::uint64_t h, std::uint32_t v) noexcept {
  return fnv1a_64(h, &v, sizeof(v));
}

// Deterministic semantic digest of a snapshot (excluding memory addresses and
// incidental ordering). Same logical state => same digest.
[[nodiscard]] inline std::uint64_t semantic_digest(const Snapshot& s) {
  std::uint64_t h = 1469598103934665603ull;
  h = fnv1a_64_u64(h, s.sequence);
  h = fnv1a_64_u64(h, s.epoch.value());
  for (const auto& n : s.nodes) {
    h = fnv1a_64_str(h, n.bdf.canonical());
    h = fnv1a_64_u32(h, static_cast<std::uint32_t>(n.kind));
    h = fnv1a_64_u64(h, n.generation.value());
    h = fnv1a_64_u32(h, static_cast<std::uint32_t>(n.provenance));
    if (n.parent) h = fnv1a_64_u64(h, n.parent->value());
    h = fnv1a_64_str(h, n.vendor);
    h = fnv1a_64_str(h, n.device);
    h = fnv1a_64_str(h, n.class_name);
  }
  for (const auto& p : s.paths) {
    h = fnv1a_64_u64(h, p.id().value());
    h = fnv1a_64_u64(h, p.generation().value());
    h = fnv1a_64_u64(h, p.source().value());
    h = fnv1a_64_u64(h, p.destination().value());
    h = fnv1a_64_u32(h, static_cast<std::uint32_t>(p.path_class()));
    h = fnv1a_64_u32(h, static_cast<std::uint32_t>(p.locality()));
    h = fnv1a_64_u32(h, static_cast<std::uint32_t>(p.freshness().state()));
    if (p.theoretical()) h = fnv1a_64_u64(h, static_cast<std::uint64_t>(p.theoretical()->effective_bytes_per_sec().value()));
    if (p.best_measured()) { double bv = p.best_measured()->value(); h = fnv1a_64(h, &bv, 8); }
  }
  for (const auto& m : s.measurements) {
    h = fnv1a_64_u64(h, m.id().value());
    h = fnv1a_64_u64(h, m.source().value());
    h = fnv1a_64_u64(h, m.destination().value());
    h = fnv1a_64_u32(h, static_cast<std::uint32_t>(m.transfer_class()));
    h = fnv1a_64_u64(h, m.bytes().value());
    h = fnv1a_64_u64(h, static_cast<std::uint64_t>(m.throughput().value()));
  }
  h = fnv1a_64_u64(h, h ^ 0x9E3779B97F4A7C15ull);
  return h;
}

}  // namespace pci
