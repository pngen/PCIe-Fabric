// SPDX-License-Identifier: Apache-2.0
// Example 15: Snapshot and diff. Take two snapshots of the same Runtime, then
// compute the deterministic diff (appeared/disappeared endpoints, generation
// changes, path changes) between them.
#include "pciefabric/runtime.hpp"
#include "pciefabric/snapshot.hpp"
#include "pciefabric/enums.hpp"
#include "pciefabric/topology.hpp"
#include <cstdio>
#include <map>
#include <set>

static bool is_ep_kind(pci::DeviceClass k) {
  return k == pci::DeviceClass::ACCELERATOR || k == pci::DeviceClass::NIC ||
         k == pci::DeviceClass::STORAGE || k == pci::DeviceClass::MEMORY ||
         k == pci::DeviceClass::OTHER;
}

static pci::SnapshotDiff snapshot_diff(const pci::Snapshot& a, const pci::Snapshot& b) {
  pci::SnapshotDiff d;
  std::map<pci::PciNodeId, pci::Bdf> aep, bep;
  std::map<pci::PciNodeId, std::uint64_t> agen, bgen;
  for (const auto& n : a.nodes) { agen[n.id] = n.generation.value();
                                  if (is_ep_kind(n.kind)) aep[n.id] = n.bdf; }
  for (const auto& n : b.nodes) { bgen[n.id] = n.generation.value();
                                  if (is_ep_kind(n.kind)) bep[n.id] = n.bdf; }
  for (const auto& kv : bep) if (!aep.count(kv.first)) d.endpoints_appeared.push_back(kv.second);
  for (const auto& kv : aep) if (!bep.count(kv.first)) d.endpoints_disappeared.push_back(kv.second);
  for (const auto& kv : bgen) {
    auto it = agen.find(kv.first);
    if (it != agen.end() && it->second != kv.second) d.generation_changed.push_back(kv.first);
  }
  std::set<pci::PathId> ap, bp;
  for (const auto& p : a.paths) ap.insert(p.id());
  for (const auto& p : b.paths) bp.insert(p.id());
  for (auto id : bp) if (!ap.count(id)) d.path_changed.push_back(id);
  for (auto id : ap) if (!bp.count(id)) d.path_changed.push_back(id);
  return d;
}

int main() {
  pci::Runtime rt;
  rt.set_authority(pci::CoordinatorEpoch(1), pci::WorkerBootId(1), pci::HostGeneration(1), 1);
  rt.add_endpoint(pci::PciNodeId(1), pci::Bdf(0, 0, 0, 0), pci::DeviceClass::ROOT_COMPLEX,
                  pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, pci::PciNodeId(0));
  rt.add_endpoint(pci::PciNodeId(2), pci::Bdf(0, 1, 0, 0), pci::DeviceClass::BRIDGE,
                  pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, pci::PciNodeId(1));
  rt.add_endpoint(pci::PciNodeId(3), pci::Bdf(0, 2, 0, 0), pci::DeviceClass::ACCELERATOR,
                  pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, pci::PciNodeId(2));
  rt.add_endpoint(pci::PciNodeId(4), pci::Bdf(0, 2, 0, 1), pci::DeviceClass::NIC,
                  pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, pci::PciNodeId(2));
  rt.build_paths();
  pci::Snapshot s1 = rt.snapshot();
  std::printf("snapshot #1: nodes=%zu paths=%zu seq=%llu digest=%llu\n",
              s1.nodes.size(), s1.paths.size(),
              static_cast<unsigned long long>(s1.sequence),
              static_cast<unsigned long long>(s1.semantic_digest));

  // A topology change: add a storage endpoint and rebuild paths.
  rt.add_endpoint(pci::PciNodeId(5), pci::Bdf(0, 3, 0, 0), pci::DeviceClass::STORAGE,
                  pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, pci::PciNodeId(2));
  rt.build_paths();
  pci::Snapshot s2 = rt.snapshot();
  std::printf("snapshot #2: nodes=%zu paths=%zu seq=%llu digest=%llu\n",
              s2.nodes.size(), s2.paths.size(),
              static_cast<unsigned long long>(s2.sequence),
              static_cast<unsigned long long>(s2.semantic_digest));

  pci::SnapshotDiff diff = snapshot_diff(s1, s2);
  std::printf("diff empty=%d endpoints_appeared=%zu endpoints_disappeared=%zu generation_changed=%zu path_changed=%zu\n",
              static_cast<int>(diff.empty()), diff.endpoints_appeared.size(),
              diff.endpoints_disappeared.size(), diff.generation_changed.size(), diff.path_changed.size());
  for (const auto& b : diff.endpoints_appeared) std::printf("  appeared endpoint: %s\n", b.canonical().c_str());
  return 0;
}
