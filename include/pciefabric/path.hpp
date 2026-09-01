#pragma once
// A resolved PCIe path between two endpoints (or an endpoint and a host domain),
// carrying structural facts, capability, theoretical envelope, measured
// evidence, locality, contention, freshness, and authority generations.
#include <cstdint>
#include <set>
#include <optional>
#include <string>
#include <vector>

#include "pciefabric/bandwidth.hpp"
#include "pciefabric/enums.hpp"
#include "pciefabric/ids.hpp"
#include "pciefabric/link.hpp"
#include "pciefabric/measurement.hpp"
#include "pciefabric/provenance.hpp"
#include "pciefabric/topology.hpp"
#include "pciefabric/values.hpp"

namespace pci {

class Path {
public:
  Path() = default;
  Path(PathId id, PathGeneration gen) : id_(id), generation_(gen) {}

  // ---- identity / generations ----
  [[nodiscard]] PathId id() const noexcept { return id_; }
  [[nodiscard]] PathGeneration generation() const noexcept { return generation_; }
  void set_generation(PathGeneration g) noexcept { generation_ = g; }

  // ---- endpoints & class ----
  void set_endpoints(PciNodeId src, PciNodeId dst) { source_ = src; destination_ = dst; }
  [[nodiscard]] PciNodeId source() const noexcept { return source_; }
  [[nodiscard]] PciNodeId destination() const noexcept { return destination_; }
  void set_path_class(PathClass c) noexcept { class_ = c; }
  [[nodiscard]] PathClass path_class() const noexcept { return class_; }

  // ---- structure ----
  void set_bridge_chain(std::vector<PciNodeId> c) { bridge_chain_ = std::move(c); }
  [[nodiscard]] const std::vector<PciNodeId>& bridge_chain() const noexcept { return bridge_chain_; }
  void set_links(std::vector<LinkId> l) { links_ = std::move(l); }
  [[nodiscard]] const std::vector<LinkId>& links() const noexcept { return links_; }
  [[nodiscard]] std::uint32_t hop_count() const noexcept {
    return static_cast<std::uint32_t>(bridge_chain_.empty() ? 0u : bridge_chain_.size() - 1u);
  }

  // ---- locality / capability ----
  void set_locality(LocalityClass l) noexcept { locality_ = l; }
  [[nodiscard]] LocalityClass locality() const noexcept { return locality_; }
  void set_theoretical(LinkEnvelope e) { theoretical_ = e; }
  [[nodiscard]] std::optional<LinkEnvelope> theoretical() const noexcept { return theoretical_; }
  void add_capability(Capability c) { caps_.insert(c); }
  [[nodiscard]] bool has_capability(Capability c) const { return caps_.contains(c); }
  void set_capabilities(std::vector<Capability> c) { caps_.clear(); for (auto x : c) caps_.insert(x); }

  // ---- measured evidence ----
  void set_best_measured(BytesPerSecond bps, Provenance p) { measured_bps_ = bps; measured_prov_ = p; }
  [[nodiscard]] std::optional<BytesPerSecond> best_measured() const { return measured_bps_; }
  [[nodiscard]] Provenance measured_provenance() const { return measured_prov_; }
  void set_measurement_history(std::vector<MeasurementId> h) { history_ = std::move(h); }
  [[nodiscard]] const std::vector<MeasurementId>& measurement_history() const noexcept { return history_; }

  // ---- freshness / contention / confidence ----
  void set_freshness(FreshnessRecord f) noexcept { freshness_ = f; }
  [[nodiscard]] FreshnessRecord freshness() const noexcept { return freshness_; }
  void set_contention(ContentionState c) noexcept { contention_ = c; }
  [[nodiscard]] ContentionState contention() const noexcept { return contention_; }
  void set_contention_score(ContentionScore s) { contention_score_ = s; }
  [[nodiscard]] ContentionScore contention_score() const { return contention_score_; }
  void set_shared_domain(std::vector<PciNodeId> s) { shared_ = std::move(s); }
  [[nodiscard]] const std::vector<PciNodeId>& shared_domain() const noexcept { return shared_; }
  void set_confidence(Confidence c) noexcept { confidence_ = c; }
  [[nodiscard]] Confidence confidence() const { return confidence_; }
  [[nodiscard]] Provenance structural_provenance() const { return structural_prov_; }
  void set_structural_provenance(Provenance p) { structural_prov_ = p; }

  // ---- authority generations (fencing envelope) ----
  void set_authority(CoordinatorEpoch epoch, WorkerBootId boot, HostGeneration host,
                     PathGeneration pathgen, DeviceGeneration devicegen) {
    epoch_ = epoch; boot_ = boot; host_gen_ = host; path_gen_ = pathgen; device_gen_ = devicegen;
  }
  [[nodiscard]] CoordinatorEpoch epoch() const { return epoch_; }
  [[nodiscard]] WorkerBootId boot() const { return boot_; }
  [[nodiscard]] HostGeneration host_generation() const { return host_gen_; }
  [[nodiscard]] DeviceGeneration device_generation() const { return device_gen_; }
  [[nodiscard]] bool authority_matches(CoordinatorEpoch e, WorkerBootId b) const {
    return epoch_ == e && boot_ == b;
  }

private:
  PathId id_{};
  PathGeneration generation_{};
  PciNodeId source_{};
  PciNodeId destination_{};
  PathClass class_{PathClass::UNKNOWN};
  std::vector<PciNodeId> bridge_chain_;
  std::vector<LinkId> links_;
  LocalityClass locality_{LocalityClass::UNKNOWN};
  std::optional<LinkEnvelope> theoretical_;
  std::set<Capability> caps_;
  std::optional<BytesPerSecond> measured_bps_;
  Provenance measured_prov_{Provenance::UNKNOWN};
  std::vector<MeasurementId> history_;
  FreshnessRecord freshness_{};
  ContentionState contention_{ContentionState::UNKNOWN};
  ContentionScore contention_score_{};
  std::vector<PciNodeId> shared_;
  Confidence confidence_{};
  Provenance structural_prov_{Provenance::REPORTED};
  CoordinatorEpoch epoch_{};
  WorkerBootId boot_{};
  HostGeneration host_gen_{};
  PathGeneration path_gen_{};
  DeviceGeneration device_gen_{};
};

// Compute the semantic path class for a pair of nodes in a topology.
[[nodiscard]] inline PathClass compute_path_class(const Topology& topo, PciNodeId src, PciNodeId dst) {
  const Node* s = topo.node(src);
  const Node* d = topo.node(dst);
  if (s == nullptr || d == nullptr) return PathClass::UNKNOWN;
  bool src_end = is_endpoint_kind(s->kind);
  bool dst_end = is_endpoint_kind(d->kind);
  bool src_root = (s->kind == DeviceClass::ROOT_COMPLEX || s->kind == DeviceClass::ROOT_PORT);
  bool dst_root = (d->kind == DeviceClass::ROOT_COMPLEX || d->kind == DeviceClass::ROOT_PORT);

  if (src_end && dst_root) {
    if (s->kind == DeviceClass::ACCELERATOR) return PathClass::ACCELERATOR_TO_HOST;
    return PathClass::ENDPOINT_TO_HOST;
  }
  if (src_root && dst_end) {
    if (d->kind == DeviceClass::ACCELERATOR) return PathClass::HOST_TO_ACCELERATOR;
    return PathClass::HOST_TO_ENDPOINT;
  }
  if (src_end && dst_end) {
    if (s->kind == DeviceClass::ACCELERATOR && d->kind == DeviceClass::NIC) return PathClass::ACCELERATOR_TO_NIC;
    if (s->kind == DeviceClass::ACCELERATOR && d->kind == DeviceClass::STORAGE) return PathClass::ACCELERATOR_TO_STORAGE;
    if (s->kind == DeviceClass::NIC && d->kind == DeviceClass::ACCELERATOR) return PathClass::NIC_TO_ACCELERATOR;
    if (s->kind == DeviceClass::STORAGE && d->kind == DeviceClass::ACCELERATOR) return PathClass::STORAGE_TO_ACCELERATOR;
    return PathClass::PEER_ENDPOINT_PATH;
  }
  return PathClass::UNKNOWN;
}

// Build a Path (structural facts only; measured/theoretical are attached later).
[[nodiscard]] inline Path build_path(const Topology& topo, PathId id, PathGeneration gen,
                                     PciNodeId src, PciNodeId dst,
                                     CoordinatorEpoch epoch, WorkerBootId boot) {
  Path p(id, gen);
  p.set_endpoints(src, dst);
  p.set_path_class(compute_path_class(topo, src, dst));
  auto chain = topo.path_nodes(src, dst);
  p.set_bridge_chain(chain);
  p.set_locality(topo.locality(src, dst));
  auto rc = topo.root_complex_of(src);
  auto rc2 = topo.root_complex_of(dst);
  // shared contention domain (structural sharing, not contention inference)
  auto shared = topo.shared_contention_node(src, dst);
  std::vector<PciNodeId> sd;
  if (shared) sd.push_back(*shared);
  p.set_shared_domain(sd);
  p.set_confidence(Confidence(0.5));
  // Link capacity / theoretical envelope is attached by the caller once link
  // capability has been correlated. Path structure here is purely structural.
  p.set_authority(epoch, boot, HostGeneration(1), gen, DeviceGeneration(1));
  return p;
}

}  // namespace pci