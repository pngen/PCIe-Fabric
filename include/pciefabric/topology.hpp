#pragma once
// PCIe hierarchy / topology model: root complexes, bridges, endpoints, their
// ancestry, locality, and shared-path (contention) domains. Structural facts
// are kept separate from measured transport behavior and from locality scores.
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "pciefabric/bdf.hpp"
#include "pciefabric/enums.hpp"
#include "pciefabric/ids.hpp"
#include "pciefabric/link.hpp"
#include "pciefabric/provenance.hpp"
#include "pciefabric/result.hpp"
#include "pciefabric/values.hpp"

namespace pci {

// An endpoint node kind (device leaf) vs. a switching/host element.
[[nodiscard]] inline bool is_endpoint_kind(DeviceClass k) noexcept {
  return k == DeviceClass::ACCELERATOR || k == DeviceClass::NIC || k == DeviceClass::STORAGE ||
         k == DeviceClass::MEMORY || k == DeviceClass::OTHER;
}

// One node in the PCIe hierarchy. A node is root complex, bridge, switch, or
// endpoint. Identity and generation are kept separate.
struct Node {
  PciNodeId id{};
  Bdf bdf{};
  DeviceClass kind{DeviceClass::UNKNOWN};
  PciNodeGeneration generation{};
  Provenance provenance{Provenance::REPORTED};
  std::optional<PciNodeId> parent{};      // upstream
  std::vector<PciNodeId> children{};    // downstream
  std::optional<LinkId> upstream_link{};
  std::vector<LinkId> downstream_links{};
  std::string vendor;
  std::string device;
  std::string class_name;
  std::string driver;
  std::optional<AcceleratorId> accelerator{};
  std::optional<NicId> nic{};
  std::optional<StorageDeviceId> storage{};
  std::optional<MemoryDeviceId> memory{};
  std::optional<std::uint32_t> numa_node{};
  bool removed{false};
};

// Thread-safe, deterministic PCIe hierarchy store.
class Topology {
public:
  Topology() = default;

  // ---- Builder ----
  Result<PciNodeId> add_root_complex(RootComplexId rc, const Bdf& bdf,
                                     PciNodeGeneration gen, Provenance src) {
    (void)rc;
    std::unique_lock lock(*mu_);
    PciNodeId id = fresh_node_id();
    return add_node_locked(id, bdf, DeviceClass::ROOT_COMPLEX, gen, src);
  }
  Result<PciNodeId> add_bridge(BridgeId b, const Bdf& bdf, PciNodeGeneration gen,
                               Provenance src, PciNodeId parent) {
    (void)b;
    std::unique_lock lock(*mu_);
    auto r = add_node_locked(fresh_node_id(), bdf, DeviceClass::BRIDGE, gen, src);
    if (r) attach_locked(r.value(), parent, std::nullopt);
    return r;
  }
  Result<PciNodeId> add_endpoint(PciNodeId id, const Bdf& bdf, DeviceClass kind,
                                 PciNodeGeneration gen, Provenance src, PciNodeId parent) {
    std::unique_lock lock(*mu_);
    auto r = add_node_locked(id, bdf, kind, gen, src);
    if (r) attach_locked(r.value(), parent, std::nullopt);
    return r;
  }
  Result<void> attach(PciNodeId child, PciNodeId parent, std::optional<LinkId> link) {
    std::unique_lock lock(*mu_);
    return attach_locked(child, parent, link);
  }
  Result<void> remove_node(PciNodeId id) {
    std::unique_lock lock(*mu_);
    if (!nodes_.contains(id)) return Result<void>::err(ErrorCode::NOT_FOUND, "node absent");
    auto& n = nodes_.at(id);
    n.removed = true;
    // refresh generation to invalidate stale references
    ++gen_;
    return Result<void>::success();
  }
  Result<void> bump_generation(PciNodeId id) {
    std::unique_lock lock(*mu_);
    if (!nodes_.contains(id)) return Result<void>::err(ErrorCode::NOT_FOUND, "node absent");
    ++nodes_.at(id).generation;
    return Result<void>::success();
  }

  // ---- Queries (thread-safe reads). ----
  [[nodiscard]] std::size_t size() const {
    std::shared_lock lock(*mu_);
    return nodes_.size();
  }
  [[nodiscard]] bool contains(PciNodeId id) const {
    std::shared_lock lock(*mu_);
    return nodes_.contains(id);
  }
  [[nodiscard]] const Node* node(PciNodeId id) const {
    std::shared_lock lock(*mu_);
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
  }
  [[nodiscard]] std::optional<PciNodeId> find_by_bdf(const Bdf& bdf) const {
    std::shared_lock lock(*mu_);
    return find_by_bdf_locked(bdf);
  }
  [[nodiscard]] std::vector<PciNodeId> ancestors(PciNodeId id) const {
    std::shared_lock lock(*mu_);
    return ancestors_locked(id);
  }
  [[nodiscard]] std::optional<PciNodeId> common_ancestor(PciNodeId a, PciNodeId b) const {
    std::shared_lock lock(*mu_);
    return common_ancestor_locked(a, b);
  }
  [[nodiscard]] std::vector<PciNodeId> path_nodes(PciNodeId a, PciNodeId b) const {
    std::shared_lock lock(*mu_);
    return path_nodes_locked(a, b);
  }
  [[nodiscard]] LocalityClass locality(PciNodeId a, PciNodeId b) const {
    std::shared_lock lock(*mu_);
    return locality_locked(a, b);
  }
  [[nodiscard]] std::optional<PciNodeId> root_complex_of(PciNodeId id) const {
    std::shared_lock lock(*mu_);
    return root_complex_of_locked(id);
  }
  [[nodiscard]] std::vector<PciNodeId> siblings(PciNodeId id) const {
    std::shared_lock lock(*mu_);
    return siblings_locked(id);
  }
  [[nodiscard]] std::vector<PciNodeId> children_of(PciNodeId id) const {
    std::shared_lock lock(*mu_);
    return children_of_locked(id);
  }
  // Deterministic, semantically-sorted enumeration (by canonical BDF).
  [[nodiscard]] std::vector<PciNodeId> all_nodes() const {
    std::shared_lock lock(*mu_);
    std::vector<PciNodeId> out;
    for (auto& [k, v] : nodes_) if (!v.removed) out.push_back(k);
    std::sort(out.begin(), out.end(), [&](PciNodeId a, PciNodeId b) {
      const Node& na = nodes_.at(a); const Node& nb = nodes_.at(b);
      if (na.bdf != nb.bdf) return na.bdf < nb.bdf;
      return a < b;
    });
    return out;
  }
  // Enumerate all endpoints.
  [[nodiscard]] std::vector<PciNodeId> endpoints() const {
    std::shared_lock lock(*mu_);
    std::vector<PciNodeId> out;
    for (auto& [k, v] : nodes_) if (!v.removed && is_endpoint_kind(v.kind)) out.push_back(k);
    std::sort(out.begin(), out.end(), [&](PciNodeId a, PciNodeId b) { return nodes_.at(a).bdf < nodes_.at(b).bdf; });
    return out;
  }
  // Structural contention domain: the common upstream bridge/root that two
  // endpoints share. Returns the shared ancestor if the nodes share one.
  [[nodiscard]] std::optional<PciNodeId> shared_contention_node(PciNodeId a, PciNodeId b) const {
    std::shared_lock lock(*mu_);
    if (a == b) return a;
    return common_ancestor_locked(a, b);
  }
  [[nodiscard]] Result<void> validate() const {
    std::shared_lock lock(*mu_);
    return validate_locked();
  }

private:
  // --- unlocked helpers (caller holds a lock) ---
  PciNodeId fresh_node_id() { return PciNodeId(next_id_++); }
  static bool is_endpoint_kind(DeviceClass k) {
    return k == DeviceClass::ACCELERATOR || k == DeviceClass::NIC || k == DeviceClass::STORAGE ||
           k == DeviceClass::MEMORY || k == DeviceClass::OTHER;
  }
  Result<PciNodeId> add_node_locked(PciNodeId id, const Bdf& bdf, DeviceClass kind,
                                    PciNodeGeneration gen, Provenance src) {
    if (auto it = find_by_bdf_locked(bdf); it) {
      // same BDF may return with a fresh generation
      auto& n = nodes_.at(*it);
      ++n.generation;
      return Result<PciNodeId>::success(*it);
    }
    if (nodes_.contains(id)) return Result<PciNodeId>::err(ErrorCode::DUPLICATE_IDENTITY, "duplicate node id");
    Node n;
    n.id = id; n.bdf = bdf; n.kind = kind; n.generation = gen; n.provenance = src;
    nodes_.emplace(id, std::move(n));
    return Result<PciNodeId>::success(id);
  }
  Result<void> attach_locked(PciNodeId child, PciNodeId parent, std::optional<LinkId> link) {
    auto pit = nodes_.find(parent);
    auto cit = nodes_.find(child);
    if (pit == nodes_.end() || cit == nodes_.end())
      return Result<void>::err(ErrorCode::NOT_FOUND, "parent/child absent");
    if (child == parent) return Result<void>::err(ErrorCode::HIERARCHY_CYCLE, "self cycle");
    auto& p = pit->second; auto& c = cit->second;
    if (c.parent && *c.parent == parent) return Result<void>::success();  // already attached
    c.parent = parent;
    if (std::find(p.children.begin(), p.children.end(), child) == p.children.end())
      p.children.push_back(child);
    if (link) { p.downstream_links.push_back(*link); c.upstream_link = *link; }
    // cycle detection
    if (would_cycle_locked(child, parent)) return Result<void>::err(ErrorCode::HIERARCHY_CYCLE, "cycle detected");
    return Result<void>::success();
  }
  bool would_cycle_locked(PciNodeId child, PciNodeId parent) const {
    // parent must not be a descendant of child
    for (auto a : ancestors_locked(parent)) if (a == child) return true;
    return false;
  }
  std::optional<PciNodeId> find_by_bdf_locked(const Bdf& b) const {
    for (auto& [k, v] : nodes_) if (!v.removed && v.bdf == b) return k;
    return std::nullopt;
  }
  std::vector<PciNodeId> ancestors_locked(PciNodeId id) const {
    std::vector<PciNodeId> out;
    std::optional<PciNodeId> cur = id;
    std::size_t guard = 0;
    while (cur && guard++ < nodes_.size() + 1) {
      out.push_back(*cur);
      auto it = nodes_.find(*cur);
      if (it == nodes_.end()) break;
      cur = it->second.parent;
    }
    return out;
  }
  std::optional<PciNodeId> common_ancestor_locked(PciNodeId a, PciNodeId b) const {
    auto aa = ancestors_locked(a);
    auto bb = ancestors_locked(b);
    for (auto x : aa) if (std::find(bb.begin(), bb.end(), x) != bb.end()) return x;
    return std::nullopt;
  }
  std::vector<PciNodeId> path_nodes_locked(PciNodeId a, PciNodeId b) const {
    auto aa = ancestors_locked(a);
    auto bb = ancestors_locked(b);
    auto ca = common_ancestor_locked(a, b);
    if (!ca) return {};
    std::vector<PciNodeId> path;
    for (auto x : aa) { path.push_back(x); if (x == *ca) break; }
    std::vector<PciNodeId> up;
    for (auto x : bb) { if (x == *ca) break; up.push_back(x); }
    std::reverse(up.begin(), up.end());
    for (auto x : up) if (x != *ca) path.push_back(x);
    return path;
  }
  LocalityClass locality_locked(PciNodeId a, PciNodeId b) const {
    if (a == b) return LocalityClass::SAME_ENDPOINT;
    auto ca = common_ancestor_locked(a, b);
    if (!ca) return LocalityClass::UNKNOWN;
    auto& n = nodes_.at(*ca);
    if (n.kind == DeviceClass::SWITCH || n.kind == DeviceClass::BRIDGE)
      return LocalityClass::SAME_SWITCH;
    if (n.kind == DeviceClass::ROOT_COMPLEX || n.kind == DeviceClass::ROOT_PORT)
      return LocalityClass::SAME_ROOT_COMPLEX;
    return LocalityClass::SAME_HOST_REMOTE_ROOT;
  }
  std::optional<PciNodeId> root_complex_of_locked(PciNodeId id) const {
    for (auto a : ancestors_locked(id)) {
      auto it = nodes_.find(a);
      if (it != nodes_.end() && (it->second.kind == DeviceClass::ROOT_COMPLEX || it->second.kind == DeviceClass::ROOT_PORT))
        return a;
    }
    return std::nullopt;
  }
  std::vector<PciNodeId> siblings_locked(PciNodeId id) const {
    auto it = nodes_.find(id);
    if (it == nodes_.end() || !it->second.parent) return {};
    return children_of_locked(*it->second.parent);
  }
  std::vector<PciNodeId> children_of_locked(PciNodeId id) const {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return {};
    return it->second.children;
  }
  Result<void> validate_locked() const {
    // cycle / orphan / duplicate current bdf
    std::unordered_map<PciBdf, PciNodeId> seen;
    for (auto& [k, v] : nodes_) {
      if (v.removed) continue;
      if (!valid(v.kind)) return Result<void>::err(ErrorCode::INVALID_VALUE, "invalid kind");
      auto pb = Bdf::to_identity(v.bdf.canonical());
      if (!pb) return Result<void>::err(ErrorCode::INVALID_BDF, "invalid bdf");
      if (seen.contains(pb.value())) return Result<void>::err(ErrorCode::DUPLICATE_CURRENT, "duplicate current bdf");
      seen.emplace(pb.value(), k);
      // cycle guarded by ancestors guard already
      if (v.parent) {
        if (!nodes_.contains(*v.parent)) return Result<void>::err(ErrorCode::INVALID_RELATIONSHIP, "dangling parent");
      }
    }
    return Result<void>::success();
  }

  std::uint64_t next_id_{1};
  PciNodeGeneration gen_{};
  mutable std::shared_ptr<std::shared_mutex> mu_{std::make_shared<std::shared_mutex>()};
  std::map<PciNodeId, Node> nodes_;
};

}  // namespace pci