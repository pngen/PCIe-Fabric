// SPDX-License-Identifier: Apache-2.0
// Benchmark 07: Snapshot and diff computation. Reports ops/s.
#include "pciefabric/runtime.hpp"
#include "pciefabric/snapshot.hpp"
#include "pciefabric/enums.hpp"
#include "pciefabric/topology.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <set>

static bool is_ep_kind(pci::DeviceClass k) {
  return k == pci::DeviceClass::ACCELERATOR || k == pci::DeviceClass::NIC ||
         k == pci::DeviceClass::STORAGE || k == pci::DeviceClass::MEMORY ||
         k == pci::DeviceClass::OTHER;
}

static pci::SnapshotDiff diff(const pci::Snapshot& a, const pci::Snapshot& b) {
  pci::SnapshotDiff d;
  std::map<pci::PciNodeId, pci::Bdf> aep, bep;
  std::map<pci::PciNodeId, std::uint64_t> agen, bgen;
  for (const auto& n : a.nodes) { agen[n.id] = n.generation.value(); if (is_ep_kind(n.kind)) aep[n.id] = n.bdf; }
  for (const auto& n : b.nodes) { bgen[n.id] = n.generation.value(); if (is_ep_kind(n.kind)) bep[n.id] = n.bdf; }
  for (const auto& kv : bep) if (!aep.count(kv.first)) d.endpoints_appeared.push_back(kv.second);
  for (const auto& kv : aep) if (!bep.count(kv.first)) d.endpoints_disappeared.push_back(kv.second);
  for (const auto& kv : bgen) { auto it = agen.find(kv.first); if (it != agen.end() && it->second != kv.second) d.generation_changed.push_back(kv.first); }
  std::set<pci::PathId> ap, bp;
  for (const auto& p : a.paths) ap.insert(p.id());
  for (const auto& p : b.paths) bp.insert(p.id());
  for (auto id : bp) if (!ap.count(id)) d.path_changed.push_back(id);
  for (auto id : ap) if (!bp.count(id)) d.path_changed.push_back(id);
  return d;
}

int main(int argc, char** argv) {
  pci::Runtime rt;
  rt.set_authority(pci::CoordinatorEpoch(1), pci::WorkerBootId(1), pci::HostGeneration(1), 1);
  rt.add_endpoint(pci::PciNodeId(1), pci::Bdf(0, 0, 0, 0), pci::DeviceClass::ROOT_COMPLEX,
                  pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, pci::PciNodeId(0));
  rt.add_endpoint(pci::PciNodeId(2), pci::Bdf(0, 1, 0, 0), pci::DeviceClass::BRIDGE,
                  pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, pci::PciNodeId(1));
  rt.add_endpoint(pci::PciNodeId(3), pci::Bdf(0, 2, 0, 0), pci::DeviceClass::STORAGE,
                  pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, pci::PciNodeId(2));
  rt.build_paths();
  pci::Snapshot s1 = rt.snapshot();
  rt.add_endpoint(pci::PciNodeId(4), pci::Bdf(0, 2, 0, 1), pci::DeviceClass::NIC,
                  pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, pci::PciNodeId(2));
  rt.build_paths();
  pci::Snapshot s2 = rt.snapshot();

  std::uint64_t n = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 100000;
  if (n == 0) n = 1;
  if (n > 2000000) n = 2000000;

  std::uint64_t sink = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < n; ++i) {
    pci::SnapshotDiff d = diff(s1, s2);
    sink += d.empty() ? 0u : d.endpoints_appeared.size();
  }
  auto t1 = std::chrono::steady_clock::now();
  double sec = std::chrono::duration<double>(t1 - t0).count();
  if (sec <= 0.0) sec = 1e-9;
  std::printf("snapshot/diff         : %llu diffs in %.3f s -> %.0f diffs/s (sink=%llu)\n",
              static_cast<unsigned long long>(n), sec, static_cast<double>(n) / sec,
              static_cast<unsigned long long>(sink));
  return 0;
}
