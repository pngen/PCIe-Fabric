#pragma once
// Measurement records and the measurement store. A measurement counts only
// operations that completed under the benchmark's synchronization contract;
// submitted-as-completed work is never counted as completed throughput.
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

#include "pciefabric/enums.hpp"
#include "pciefabric/ids.hpp"
#include "pciefabric/provenance.hpp"
#include "pciefabric/values.hpp"

namespace pci {

// A single completed-transfer measurement.
class Measurement {
public:
  Measurement() = default;
  Measurement(MeasurementId id, MeasurementGeneration gen, PciNodeId src, PciNodeId dst,
              TransferClass cls, EngineKind backend, TransferBytes bytes,
              IterationCount iters, Nanoseconds duration_ns,
              Provenance prov);

  [[nodiscard]] MeasurementId id() const noexcept { return id_; }
  [[nodiscard]] MeasurementGeneration generation() const noexcept { return generation_; }
  [[nodiscard]] PciNodeId source() const noexcept { return source_; }
  [[nodiscard]] PciNodeId destination() const noexcept { return destination_; }
  [[nodiscard]] TransferClass transfer_class() const noexcept { return transfer_class_; }
  [[nodiscard]] EngineKind engine() const noexcept { return engine_; }
  [[nodiscard]] TransferBytes bytes() const noexcept { return bytes_; }
  [[nodiscard]] IterationCount iterations() const noexcept { return iterations_; }
  [[nodiscard]] Nanoseconds duration_ns() const noexcept { return duration_ns_; }
  [[nodiscard]] Nanoseconds latency_ns() const noexcept { return latency_ns_; }
  [[nodiscard]] Provenance provenance() const noexcept { return provenance_; }
  [[nodiscard]] std::optional<PathId> path() const noexcept { return path_; }
  [[nodiscard]] std::optional<PathGeneration> path_generation() const noexcept { return path_generation_; }
  [[nodiscard]] std::string sync_semantics() const noexcept { return sync_semantics_; }
  [[nodiscard]] std::uint64_t warmup_ops() const noexcept { return warmup_ops_; }
  [[nodiscard]] std::string backend_detail() const noexcept { return backend_detail_; }

  // MEASURED completed-transfer bytes/sec (denominator = duration).
  [[nodiscard]] BytesPerSecond throughput() const noexcept {
    double ns = static_cast<double>(duration_ns_.value());
    if (ns <= 0) return BytesPerSecond(0.0);
    return BytesPerSecond(static_cast<double>(bytes_.value()) * 1.0e9 / ns);
  }
  [[nodiscard]] GiBPerSecond throughput_gib() const noexcept {
    return GiBPerSecond(to_gib_per_sec(throughput()));
  }

  void set_latency(Nanoseconds v) noexcept { latency_ns_ = v; }
  void set_path(PathId p, PathGeneration g) { path_ = p; path_generation_ = g; }
  void set_sync_semantics(std::string s) { sync_semantics_ = std::move(s); }
  void set_warmup_ops(std::uint64_t n) noexcept { warmup_ops_ = n; }
  void set_backend_detail(std::string s) { backend_detail_ = std::move(s); }
  void set_start_end(EpochMs s, EpochMs e) { start_ = s; end_ = e; }
  [[nodiscard]] EpochMs start_ms() const noexcept { return start_; }
  [[nodiscard]] EpochMs end_ms() const noexcept { return end_; }

private:
  MeasurementId id_{};
  MeasurementGeneration generation_{};
  PciNodeId source_{};
  PciNodeId destination_{};
  TransferClass transfer_class_{TransferClass::UNKNOWN};
  EngineKind engine_{EngineKind::NONE};
  TransferBytes bytes_{};
  IterationCount iterations_{};
  Nanoseconds duration_ns_{};
  Nanoseconds latency_ns_{};
  Provenance provenance_{Provenance::UNKNOWN};
  std::optional<PathId> path_{};
  std::optional<PathGeneration> path_generation_{};
  std::string sync_semantics_;
  std::uint64_t warmup_ops_{};
  std::string backend_detail_;
  EpochMs start_{};
  EpochMs end_{};
};

inline Measurement::Measurement(MeasurementId id, MeasurementGeneration gen, PciNodeId src,
                                PciNodeId dst, TransferClass cls, EngineKind backend,
                                TransferBytes bytes, IterationCount iters,
                                Nanoseconds duration_ns, Provenance prov)
    : id_(id), generation_(gen), source_(src), destination_(dst), transfer_class_(cls),
      engine_(backend), bytes_(bytes), iterations_(iters), duration_ns_(duration_ns),
      provenance_(prov) {}

// Thread-safe measurement store with generation fencing.
class MeasurementStore {
public:
  Result<void> ingest(Measurement m) {
    std::unique_lock lock(mu_);
    auto it = entries_.find(m.id());
    if (it != entries_.end()) {
      // accept only a newer generation from the same id
      if (it->second.generation() >= m.generation())
        return Result<void>::err(ErrorCode::STALE_AUTHORITY, "measurement generation not newer");
      it->second = m;
      return Result<void>::success();
    }
    entries_.emplace(m.id(), std::move(m));
    return Result<void>::success();
  }
  [[nodiscard]] std::optional<Measurement> get(MeasurementId id) const {
    std::shared_lock lock(mu_);
    auto it = entries_.find(id);
    if (it == entries_.end()) return std::nullopt;
    return it->second;
  }
  // Latest accepted measurement for a (source,dest,class) tuple.
  [[nodiscard]] std::optional<Measurement> latest(PciNodeId src, PciNodeId dst,
                                                   TransferClass cls) const {
    std::shared_lock lock(mu_);
    std::optional<Measurement> best;
    for (auto& [k, m] : entries_) {
      (void)k;
      if (m.source() == src && m.destination() == dst && m.transfer_class() == cls) {
        if (!best || m.generation() > best->generation()) best = m;
      }
    }
    return best;
  }
  [[nodiscard]] std::size_t size() const {
    std::shared_lock lock(mu_);
    return entries_.size();
  }
  [[nodiscard]] std::vector<Measurement> all() const {
    std::shared_lock lock(mu_);
    std::vector<Measurement> out;
    for (auto& [k, m] : entries_) { (void)k; out.push_back(m); }
    return out;
  }
  [[nodiscard]] std::size_t count_for_path(PathId p) const {
    std::shared_lock lock(mu_);
    std::size_t n = 0;
    for (auto& [k, m] : entries_) { (void)k; if (m.path() && *m.path() == p) ++n; }
    return n;
  }

private:
  mutable std::shared_mutex mu_;
  std::map<MeasurementId, Measurement> entries_;
};

}  // namespace pci
