#pragma once
// The PCIe Fabric runtime facade: composes topology, path building, selection,
// reservations, measurement ingest, freshness aging, authority fencing, and
// persistence into one thread-safe, deterministic runtime.
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "pciefabric/measurement.hpp"
#include "pciefabric/path.hpp"
#include "pciefabric/persistence.hpp"
#include "pciefabric/reservation.hpp"
#include "pciefabric/selection.hpp"
#include "pciefabric/snapshot.hpp"
#include "pciefabric/topology.hpp"
#include "pciefabric/windows_enum.hpp"

namespace pci {

class Runtime {
public:
  Runtime() : reservations_(ReservationLimits{CapacityBytes(1ull << 40), Milliseconds(3600000)}) {}

  void set_authority(CoordinatorEpoch epoch, WorkerBootId boot, HostGeneration host,
                     std::uint64_t host_gen) {
    std::unique_lock lk(mu_);
    epoch_ = epoch; boot_ = boot; host_gen_ = host;
    host_gen_u64_ = host_gen;
  }

  // ---- topology building ----
  Result<PciNodeId> add_endpoint(PciNodeId id, const Bdf& bdf, DeviceClass kind,
                            PciNodeGeneration gen, Provenance prov, PciNodeId parent) {
    std::unique_lock lk(mu_);
    auto r = topology_.add_endpoint(id, bdf, kind, gen, prov, parent);
    if (r) ++topology_seq_;
    return r;
  }
  Result<void> discover_windows() {
    auto topo = build_topology_from_windows();
    if (!topo) return Result<void>::err(topo.code(), topo.message());
    std::unique_lock lk(mu_);
    topology_ = std::move(topo.value());
    ++topology_seq_;
    return Result<void>::success();
  }
  Result<void> build_paths() {
    std::unique_lock lk(mu_);
    auto eps = topology_.endpoints();
    std::vector<Path> ps;
    for (std::size_t i = 0; i < eps.size(); ++i)
      for (std::size_t j = i + 1; j < eps.size(); ++j) {
        PathId id(static_cast<std::uint64_t>(i * eps.size() + j + 1));
        Path p = build_path(topology_, id, PathGeneration(1), eps[i], eps[j], epoch_, boot_);
        FreshnessRecord f(Freshness::CURRENT, 0);
        p.set_freshness(f);
        paths_[p.id()] = p;
      }
    // also endpoint-to-host paths (root complex anchor)
    for (auto e : eps) {
      PathId id(static_cast<std::uint64_t>(0xF0000000ull + e.value()));
      auto rc = topology_.root_complex_of(e);
      if (rc) {
        Path p = build_path(topology_, id, PathGeneration(1), e, *rc, epoch_, boot_);
        p.set_freshness(FreshnessRecord(Freshness::CURRENT, 0));
        paths_[p.id()] = p;
      }
    }
    return Result<void>::success();
  }

  // ---- query ----
  [[nodiscard]] const Topology& topology() const { std::shared_lock lk(mu_); return topology_; }
  [[nodiscard]] std::vector<Path> all_paths() const {
    std::shared_lock lk(mu_);
    std::vector<Path> out; for (auto& [k,p] : paths_) { (void)k; out.push_back(p); } return out;
  }
  [[nodiscard]] std::optional<Path> find_path(PathId id) const {
    std::shared_lock lk(mu_);
    auto it = paths_.find(id); if (it == paths_.end()) return std::nullopt; return it->second;
  }
  [[nodiscard]] std::vector<Path> candidates(PciNodeId src, PciNodeId dst) const {
    std::shared_lock lk(mu_);
    std::vector<Path> out;
    for (auto& [k,p] : paths_) { (void)k; if (p.source()==src && p.destination()==dst) out.push_back(p); }
    return out;
  }

  // ---- selection / reservation ----
  SelectionDecision select(const SelectionRequest& req) {
    std::unique_lock lk(mu_);
    auto cand = candidates_locked(req.source, req.destination);
    return selector_.select(cand, req);
  }
  Result<Reservation> reserve(const Reservation& req) {
    std::unique_lock lk(mu_);
    auto r = reservations_.reserve(req);
    if (r) ++reservation_seq_;
    return r;
  }
  Result<void> release(ReservationId id, ReservationGeneration gen) {
    std::unique_lock lk(mu_);
    return reservations_.release(id, boot_, gen, epoch_);
  }
  Result<void> ingest_measurement(Measurement m) {
    std::unique_lock lk(mu_);
    m.set_path(PathId(0), PathGeneration(0));
    auto r = measurements_.ingest(m);
    if (r) ++measurement_seq_;
    return r;
  }
  // Attach best-measured evidence for each path (from the measurement store).
  Result<void> refresh_measured() {
    std::unique_lock lk(mu_);
    for (auto& [k, p] : paths_) {
      auto m = measurements_.latest(p.source(), p.destination(), TransferClass::PINNED_H2D);
      if (!m) m = measurements_.latest(p.source(), p.destination(), TransferClass::PAGEABLE_H2D);
      if (m) p.set_best_measured(m->throughput(), Provenance::MEASURED);
    }
    return Result<void>::success();
  }

  // ---- freshness aging ----
  void age_freshness() {
    std::unique_lock lk(mu_);
    for (auto& [k, p] : paths_) {
      auto f = p.freshness();
      if (f.state() == Freshness::CURRENT) p.set_freshness(FreshnessRecord(Freshness::AGING, f.age_ms()+1));
      else if (f.state() == Freshness::AGING && f.age_ms() > 60) p.set_freshness(FreshnessRecord(Freshness::STALE, f.age_ms()+1));
      else p.set_freshness(FreshnessRecord(f.state(), f.age_ms()+1));
    }
  }

  // ---- snapshot / persistence ----
  Snapshot snapshot() const {
    std::shared_lock lk(mu_);
    Snapshot s; s.epoch = epoch_; s.sequence = topology_seq_;
    for (auto id : topology_.all_nodes()) { const Node* n = topology_.node(id); if (n) s.nodes.push_back(*n); }
    for (auto& [k,p] : paths_) s.paths.push_back(p);
    for (auto& m : measurements_.all()) s.measurements.push_back(m);
    s.semantic_digest = semantic_digest(s);
    return s;
  }
  Result<void> save(const std::string& path) const {
    return persist::save(path, snapshot());
  }
  Result<Snapshot> load(const std::string& path) {
    auto snap = persist::load(path);
    if (!snap) return snap;
    // recovered physical observations require revalidation
    auto s = snap.value();
    persist::mark_physical_revalidation_required(s);
    std::unique_lock lk(mu_);
    topology_ = Topology();
    for (const auto& n : s.nodes) topology_.add_endpoint(n.id, n.bdf, n.kind, n.generation, n.provenance, n.parent ? *n.parent : PciNodeId(0));
    paths_.clear();
    for (const auto& p : s.paths) paths_[p.id()] = p;
    epoch_ = s.epoch;
    return Result<Snapshot>::success(std::move(s));
  }
  Result<void> validate_accounting() const {
    std::shared_lock lk(mu_);
    return reservations_.validate_accounting();
  }
  std::size_t path_count() const { std::shared_lock lk(mu_); return paths_.size(); }
  std::size_t endpoint_count() const { std::shared_lock lk(mu_); return topology_.endpoints().size(); }

private:
  std::vector<Path> candidates_locked(PciNodeId s, PciNodeId d) const {
    std::vector<Path> out;
    for (auto& [k,p] : paths_) { (void)k; if (p.source()==s && p.destination()==d) out.push_back(p); }
    return out;
  }
  mutable std::shared_mutex mu_;
  Topology topology_;
  std::map<PathId, Path> paths_;
  MeasurementStore measurements_;
  ReservationRegistry reservations_;
  PathSelector selector_;
  CoordinatorEpoch epoch_{};
  WorkerBootId boot_{};
  HostGeneration host_gen_{};
  std::uint64_t host_gen_u64_{};
  std::uint64_t topology_seq_{};
  std::uint64_t measurement_seq_{};
  std::uint64_t reservation_seq_{};
};

}  // namespace pci
