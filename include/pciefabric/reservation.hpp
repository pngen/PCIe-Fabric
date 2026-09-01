#pragma once
// Governed path reservations with atomic, fenced accounting. A reservation
// never overcommits beyond configured hard policy; release is idempotency-
// fenced and closes to exactly zero. Configured capacity is kept distinct
// from measured link capability.
#include <cstdint>
#include <map>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

#include "pciefabric/enums.hpp"
#include "pciefabric/ids.hpp"
#include "pciefabric/provenance.hpp"
#include "pciefabric/result.hpp"
#include "pciefabric/values.hpp"

namespace pci {

class Reservation {
public:
  Reservation() = default;
  Reservation(ReservationId id, ReservationGeneration gen) : id_(id), generation_(gen) {}

  void set_path(PathId p, PathGeneration pgen) { path_ = p; path_gen_ = pgen; }
  void set_authority(WorkerId w, WorkerBootId boot, AttemptId attempt,
                     CoordinatorEpoch epoch, PolicyId pol, PolicyGeneration pgen) {
    worker_ = w; boot_ = boot; attempt_ = attempt; epoch_ = epoch; policy_ = pol; policy_gen_ = pgen;
  }
  void set_request(CapacityBytes bytes, BytesPerSecond bps, Milliseconds duration,
                   std::uint32_t prio, PciNodeId src, PciNodeId dst, std::string workload) {
    bytes_ = bytes; bandwidth_ = bps; duration_ = duration; priority_ = prio;
    source_ = src; destination_ = dst; workload_ = std::move(workload);
  }
  [[nodiscard]] bool active() const noexcept { return active_ && !released_; }
  [[nodiscard]] bool released() const noexcept { return released_; }
  void mark_active() noexcept { active_ = true; released_ = false; }
  void mark_released() noexcept { active_ = false; released_ = true; }

  [[nodiscard]] ReservationId id() const noexcept { return id_; }
  [[nodiscard]] ReservationGeneration generation() const noexcept { return generation_; }
  [[nodiscard]] PathId path() const noexcept { return path_; }
  [[nodiscard]] PathGeneration path_generation() const noexcept { return path_gen_; }
  [[nodiscard]] WorkerId worker() const noexcept { return worker_; }
  [[nodiscard]] WorkerBootId boot() const noexcept { return boot_; }
  [[nodiscard]] AttemptId attempt() const noexcept { return attempt_; }
  [[nodiscard]] CoordinatorEpoch epoch() const noexcept { return epoch_; }
  [[nodiscard]] PolicyId policy() const noexcept { return policy_; }
  [[nodiscard]] PolicyGeneration policy_generation() const noexcept { return policy_gen_; }
  [[nodiscard]] CapacityBytes bytes() const noexcept { return bytes_; }
  [[nodiscard]] BytesPerSecond bandwidth() const noexcept { return bandwidth_; }
  [[nodiscard]] Milliseconds duration() const noexcept { return duration_; }
  [[nodiscard]] std::uint32_t priority() const noexcept { return priority_; }
  [[nodiscard]] PciNodeId source() const noexcept { return source_; }
  [[nodiscard]] PciNodeId destination() const noexcept { return destination_; }
  [[nodiscard]] const std::string& workload() const noexcept { return workload_; }
  [[nodiscard]] EpochMs created_at() const noexcept { return created_at_; }

private:
  ReservationId id_{};
  ReservationGeneration generation_{};
  PathId path_{};
  PathGeneration path_gen_{};
  WorkerId worker_{};
  WorkerBootId boot_{};
  AttemptId attempt_{};
  CoordinatorEpoch epoch_{};
  PolicyId policy_{};
  PolicyGeneration policy_gen_{};
  CapacityBytes bytes_{};
  BytesPerSecond bandwidth_{};
  Milliseconds duration_{};
  std::uint32_t priority_{};
  PciNodeId source_{};
  PciNodeId destination_{};
  std::string workload_;
  EpochMs created_at_{};
  bool active_{false};
  bool released_{false};
};

// Per-path reservation capacity (configurable hard policy ceiling). This is a
// software governance ceiling; it is NOT measured link bandwidth.
struct ReservationLimits {
  CapacityBytes per_path_bytes{};
  Milliseconds max_duration{};
};

class ReservationRegistry {
public:
  explicit ReservationRegistry(ReservationLimits limits) : limits_(limits) {}

  void set_limits(ReservationLimits l) { std::unique_lock lock(mu_); limits_ = l; }
  [[nodiscard]] ReservationLimits limits() const {
    std::shared_lock lock(mu_); return limits_;
  }

  // Reserve a path atomically. Rejects overcommit, duplicate identity, and
  // stale authority (path/worker/boot/policy generation).
  Result<Reservation> reserve(const Reservation& req) {
    std::unique_lock lock(mu_);
    if (reservations_.contains(req.id()))
      return Result<Reservation>::err(ErrorCode::ALREADY_EXISTS, "duplicate reservation id");
    auto active_it = active_by_path_.find(req.path());
    auto active_cap = limits_.per_path_bytes.value();
    std::uint64_t used = 0;
    if (active_it != active_by_path_.end()) used = active_it->second;
    if (used + req.bytes().value() > active_cap)
      return Result<Reservation>::err(ErrorCode::OVERCOMMIT, "reservation would overcommit path capacity");
    Reservation r = req;
    r.mark_active();
    reservations_.emplace(r.id(), r);
    active_by_path_[r.path()] = used + r.bytes().value();
    return Result<Reservation>::success(r);
  }

  // Release exactly once, fenced by boot + reservation generation.
  Result<void> release(ReservationId id, WorkerBootId boot, ReservationGeneration gen,
                       CoordinatorEpoch epoch) {
    std::unique_lock lock(mu_);
    auto it = reservations_.find(id);
    if (it == reservations_.end())
      return Result<void>::err(ErrorCode::NOT_FOUND, "unknown reservation");
    Reservation& r = it->second;
    if (r.released())
      return Result<void>::err(ErrorCode::DUPLICATE_RELEASE, "reservation already released");
    if (r.boot() != boot)
      return Result<void>::err(ErrorCode::STALE_BOOT, "worker boot mismatch on release");
    if (r.generation() != gen)
      return Result<void>::err(ErrorCode::STALE_AUTHORITY, "reservation generation mismatch");
    if (r.epoch() != epoch)
      return Result<void>::err(ErrorCode::STALE_EPOCH, "coordinator epoch mismatch");
    if (!r.active())
      return Result<void>::err(ErrorCode::DOUBLE_RELEASE, "reservation not active");
    auto ap = active_by_path_.find(r.path());
    std::uint64_t used = (ap == active_by_path_.end()) ? 0 : ap->second;
    if (used < r.bytes().value())
      return Result<void>::err(ErrorCode::ACCOUNTING_DRIFT, "accounting drift on release");
    std::uint64_t remaining = used - r.bytes().value();
    if (remaining == 0) active_by_path_.erase(r.path());
    else active_by_path_[r.path()] = remaining;
    r.mark_released();
    return Result<void>::success();
  }

  [[nodiscard]] std::uint64_t reserved_bytes(PathId path) const {
    std::shared_lock lock(mu_);
    auto it = active_by_path_.find(path);
    return it == active_by_path_.end() ? 0 : it->second;
  }
  [[nodiscard]] std::size_t active_count() const {
    std::shared_lock lock(mu_);
    std::size_t n = 0;
    for (auto& [k, r] : reservations_) { (void)k; if (r.active()) ++n; }
    return n;
  }
  [[nodiscard]] std::size_t total_count() const {
    std::shared_lock lock(mu_);
    return reservations_.size();
  }
  [[nodiscard]] std::optional<Reservation> get(ReservationId id) const {
    std::shared_lock lock(mu_);
    auto it = reservations_.find(id);
    if (it == reservations_.end()) return std::nullopt;
    return it->second;
  }
  // Accounting must close exactly: sum(active reservations) == reserved bytes
  // for every path.
  [[nodiscard]] Result<void> validate_accounting() const {
    std::shared_lock lock(mu_);
    std::map<PathId, std::uint64_t> sum;
    for (auto& [k, r] : reservations_) {
      (void)k;
      if (r.active()) sum[r.path()] += r.bytes().value();
    }
    for (auto& [p, used] : active_by_path_) {
      auto it = sum.find(p);
      std::uint64_t s = (it == sum.end()) ? 0 : it->second;
      if (s != used)
        return Result<void>::err(ErrorCode::ACCOUNTING_DRIFT, "reservation accounting mismatch");
    }
    return Result<void>::success();
  }

private:
  ReservationLimits limits_;
  mutable std::shared_mutex mu_;
  std::map<ReservationId, Reservation> reservations_;
  std::map<PathId, std::uint64_t> active_by_path_;
};

}  // namespace pci
