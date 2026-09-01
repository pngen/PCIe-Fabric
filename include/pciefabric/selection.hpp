#pragma once
// Deterministic path-selection engine. Ranking is component-wise (never a single
// opaque master score); the decision exposes hard constraints, eliminated and
// alternative paths, the binding constraint, and what would change the result.
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pciefabric/enums.hpp"
#include "pciefabric/ids.hpp"
#include "pciefabric/path.hpp"
#include "pciefabric/result.hpp"
#include "pciefabric/values.hpp"

namespace pci {

struct SelectionRequest {
  PciNodeId source{};
  PciNodeId destination{};
  TransferBytes payload_size{};
  std::uint32_t priority{};
  std::vector<Capability> required_capabilities;
  CoordinatorEpoch epoch{};
  WorkerBootId boot{};
  PolicyGeneration policy_generation{};
  bool want_reservation{false};
  bool prefer_local{true};
};

struct SelectionDecision {
  Decision decision{Decision::REJECT};
  std::optional<PathId> selected{};
  std::vector<PathId> alternatives;
  std::vector<PathId> eliminated;
  std::vector<PathId> reranked;         // candidates examined in ranked order
  std::string binding_constraint;
  std::vector<std::string> reasons;
  LocalityClass locality{LocalityClass::UNKNOWN};
  std::optional<LinkEnvelope> theoretical;
  std::optional<BytesPerSecond> measured;
  Confidence confidence{};
  ContentionState contention{ContentionState::UNKNOWN};
};

// Reserve a slot marker removed.
struct RankComponents {
  int admissible{0};       // 0 = not admissible (freshness/authority)
  int capability{1};       // 0 = missing required capability
  int locality{5};         // rank index into locality ordering
  std::uint64_t measured{0};
  int contention{3};
  std::uint64_t confidence_bits{0};
  int freshness{0};
  PciNodeId id{};
};

[[nodiscard]] inline int locality_rank(LocalityClass l) noexcept {
  switch (l) {
    case LocalityClass::SAME_ENDPOINT: return 0;
    case LocalityClass::SAME_SWITCH: return 1;
    case LocalityClass::SAME_ROOT_COMPLEX: return 2;
    case LocalityClass::SAME_NUMA_DOMAIN: return 3;
    case LocalityClass::SAME_HOST_REMOTE_ROOT: return 4;
    case LocalityClass::CROSS_NUMA: return 5;
    case LocalityClass::UNKNOWN: return 6;
    case LocalityClass::SYNTHETIC: return 7;
  }
  return 6;
}
[[nodiscard]] inline int contention_rank(ContentionState c) noexcept {
  switch (c) {
    case ContentionState::IDLE: return 0;
    case ContentionState::LOW: return 1;
    case ContentionState::MODERATE: return 2;
    case ContentionState::HIGH: return 3;
    case ContentionState::SATURATED: return 4;
    case ContentionState::UNKNOWN: return 2;
  }
  return 2;
}
[[nodiscard]] inline int freshness_rank(Freshness f) noexcept {
  switch (f) {
    case Freshness::CURRENT: return 0;
    case Freshness::AGING: return 1;
    case Freshness::REVALIDATION_REQUIRED: return 2;
    case Freshness::STALE: return 3;
    case Freshness::INVALID: return 4;
  }
  return 4;
}

// Component-wise comparator. Returns negative if a ranks before b.
[[nodiscard]] inline int compare_components(const Path& a, const Path& b,
                                            const SelectionRequest& req) {
  // admissible first
  int fa = freshness_rank(a.freshness().state());
  int fb = freshness_rank(b.freshness().state());
  bool adm_a = fa <= 1 && (a.epoch() == req.epoch) && (a.boot() == req.boot);
  bool adm_b = fb <= 1 && (b.epoch() == req.epoch) && (b.boot() == req.boot);
  if (adm_a != adm_b) return adm_a ? -1 : 1;
  if (!adm_a && !adm_b) return a.id() < b.id() ? -1 : (a.id() == b.id() ? 0 : 1);

  // required capability presence
  bool cap_a = true;
  bool cap_b = true;
  for (auto c : req.required_capabilities) {
    cap_a = cap_a && a.has_capability(c);
    cap_b = cap_b && b.has_capability(c);
  }
  if (cap_a != cap_b) return cap_a ? -1 : 1;

  // locality
  int la = locality_rank(a.locality());
  int lb = locality_rank(b.locality());
  if (la != lb) return la < lb ? -1 : 1;

  // measured throughput (higher better); else theoretical envelope
  std::uint64_t ma = a.best_measured() ? static_cast<std::uint64_t>(a.best_measured()->value()) : 0;
  std::uint64_t mb = b.best_measured() ? static_cast<std::uint64_t>(b.best_measured()->value()) : 0;
  if (ma != mb) return ma > mb ? -1 : 1;
  double ta = a.theoretical() ? a.theoretical()->effective_bytes_per_sec().value() : 0.0;
  double tb = b.theoretical() ? b.theoretical()->effective_bytes_per_sec().value() : 0.0;
  if (ta != tb) return ta > tb ? -1 : 1;

  // contention (lower better)
  int ca = contention_rank(a.contention());
  int cb = contention_rank(b.contention());
  if (ca != cb) return ca < cb ? -1 : 1;

  // confidence (higher better)
  double const conf_a = a.confidence().value();
  double const conf_b = b.confidence().value();
  if (conf_a != conf_b) return conf_a > conf_b ? -1 : 1;

  // deterministic final tie-break: path id / source ordering
  if (a.source() != b.source()) return a.source() < b.source() ? -1 : 1;
  if (a.destination() != b.destination()) return a.destination() < b.destination() ? -1 : 1;
  if (a.id() != b.id()) return a.id() < b.id() ? -1 : 1;
  return 0;
}

class PathSelector {
public:
  // Deterministically rank candidate paths for a request.
  [[nodiscard]] std::vector<PathId> rank(const std::vector<Path>& candidates,
                                          const SelectionRequest& req) const {
    std::vector<Path> sorted = candidates;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&](const Path& a, const Path& b) { return compare_components(a, b, req) < 0; });
    std::vector<PathId> out;
    for (auto& p : sorted) out.push_back(p.id());
    return out;
  }

  // Choose among candidates. Deterministic, component-retained.
  [[nodiscard]] SelectionDecision select(const std::vector<Path>& candidates,
                                          const SelectionRequest& req) const {
    SelectionDecision d;
    std::vector<Path> admissible;
    std::vector<Path> eliminated;
    std::vector<std::string> elim_reasons;
    std::vector<Path> deffered;

    for (auto& p : candidates) {
      bool fresh = freshness_rank(p.freshness().state()) <= 1;
      bool auth = p.epoch() == req.epoch && p.boot() == req.boot;
      bool cap = true;
      for (auto c : req.required_capabilities) cap = cap && p.has_capability(c);
      if (!auth) {
        eliminated.push_back(p);
        elim_reasons.push_back("stale authority (epoch/boot mismatch) on path " + std::to_string(p.id().value()));
        continue;
      }
      if (!cap) {
        eliminated.push_back(p);
        elim_reasons.push_back("required capability unavailable on path");
        continue;
      }
      if (!fresh) {
        d.eliminated.push_back(p.id());
        elim_reasons.push_back("path not fresh (revalidation required)");
        continue;
      }
      // saturation / capacity is handled by reservation, but a saturated
      // contention state defers.
      if (p.contention() == ContentionState::SATURATED) {
        deffered.push_back(p);
        continue;
      }
      admissible.push_back(p);
    }

    std::vector<Path> pool = admissible;
    std::stable_sort(pool.begin(), pool.end(),
                     [&](const Path& a, const Path& b) { return compare_components(a, b, req) < 0; });

    if (pool.empty()) {
      d.decision = deffered.empty() ? Decision::REJECT : Decision::DEFER;
      if (!deffered.empty()) {
        d.binding_constraint = "all candidate paths saturated";
        d.reasons.push_back("deferred: contention saturation");
        for (auto& p : deffered) d.alternatives.push_back(p.id());
      } else {
        d.binding_constraint = elim_reasons.empty() ? "no admissible path" : elim_reasons.front();
        d.reasons = elim_reasons;
      }
      d.locality = LocalityClass::UNKNOWN;
      return d;
    }

    const Path& best = pool.front();
    d.selected = best.id();
    d.decision = Decision::USE_PATH;
    d.locality = best.locality();
    d.theoretical = best.theoretical();
    d.measured = best.best_measured();
    d.confidence = best.confidence();
    d.contention = best.contention();

    // penalty if the best has degraded contention/cross-numa or weaker locality.
    if (best.contention() == ContentionState::MODERATE || best.contention() == ContentionState::HIGH ||
        best.locality() == LocalityClass::CROSS_NUMA ||
        best.locality() == LocalityClass::SAME_HOST_REMOTE_ROOT) {
      d.decision = Decision::USE_PATH_WITH_PENALTY;
      d.binding_constraint = best.contention() == ContentionState::HIGH ? "contention penalty"
                           : best.locality() == LocalityClass::CROSS_NUMA ? "cross-NUMA penalty"
                           : "remote-root locality penalty";
      d.reasons.push_back(d.binding_constraint);
    } else {
      d.binding_constraint = "best component-wise ranking";
      d.reasons.push_back("selected best in component-wise order");
    }

    for (std::size_t i = 1; i < pool.size(); ++i) d.alternatives.push_back(pool[i].id());
    for (std::size_t i = 0; i < eliminated.size(); ++i) d.eliminated.push_back(eliminated[i].id());
    for (auto& p : deffered) d.alternatives.push_back(p.id());
    for (auto& p : pool) d.reranked.push_back(p.id());
    return d;
  }
};

}  // namespace pci
