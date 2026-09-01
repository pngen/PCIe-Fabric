// SPDX-License-Identifier: Apache-2.0
// Benchmark 08: Synthetic path planning - build all peer paths in a many-endpoint
// topology and select among them. Reports plans/s (paths built).
#include "pciefabric/topology.hpp"
#include "pciefabric/path.hpp"
#include "pciefabric/selection.hpp"
#include "pciefabric/values.hpp"
#include "pciefabric/provenance.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

int main(int argc, char** argv) {
  pci::Topology topo;
  auto rc = topo.add_root_complex(pci::RootComplexId(1), pci::Bdf(0, 0, 0, 0),
                                  pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC);
  auto sw = topo.add_bridge(pci::BridgeId(2), pci::Bdf(0, 1, 0, 0),
                            pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, rc.value());
  const std::uint64_t n_ep = 16;
  std::vector<pci::PciNodeId> eps;
  for (std::uint64_t i = 0; i < n_ep; ++i) {
    pci::Bdf b(0, 2, static_cast<std::uint8_t>(i & 0x1f), static_cast<std::uint8_t>(i >> 5));
    auto id = topo.add_endpoint(pci::PciNodeId(100 + i), b, pci::DeviceClass::NIC,
                                pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, sw.value());
    eps.push_back(id.value());
  }

  // Pre-build the candidate path set for a selected peer pair.
  pci::PathId next_id(1);
  std::vector<pci::Path> candidates;
  for (std::uint64_t i = 0; i + 1 < n_ep; ++i) {
    for (std::uint64_t j = i + 1; j < n_ep; ++j) {
      pci::Path p = pci::build_path(topo, next_id, pci::PathGeneration(1), eps[i], eps[j],
                                    pci::CoordinatorEpoch(7), pci::WorkerBootId(3));
      p.set_freshness(pci::FreshnessRecord(pci::Freshness::CURRENT, 0));
      p.set_contention(pci::ContentionState::IDLE);
      candidates.push_back(p);
      next_id = pci::PathId(next_id.value() + 1);
    }
  }

  pci::SelectionRequest req;
  req.source = eps[0]; req.destination = eps[1];
  req.epoch = pci::CoordinatorEpoch(7); req.boot = pci::WorkerBootId(3);
  pci::PathSelector sel;

  std::uint64_t n = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 200;
  if (n == 0) n = 1;
  if (n > 5000) n = 5000;

  std::uint64_t sink = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < n; ++i) {
    auto d = sel.select(candidates, req);
    sink += d.selected ? d.selected->value() : 0u;
  }
  auto t1 = std::chrono::steady_clock::now();
  double sec = std::chrono::duration<double>(t1 - t0).count();
  if (sec <= 0.0) sec = 1e-9;
  std::printf("synthetic path planning: %llu plans over %zu candidates in %.3f s -> %.0f plans/s (sink=%llu)\n",
              static_cast<unsigned long long>(n), candidates.size(), sec,
              static_cast<double>(n) / sec, static_cast<unsigned long long>(sink));
  return 0;
}
