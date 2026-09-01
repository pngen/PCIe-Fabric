// SPDX-License-Identifier: Apache-2.0
// Benchmark 03: Path ranking/selection over a fixed candidate set. Reports ops/s.
#include "pciefabric/path.hpp"
#include "pciefabric/selection.hpp"
#include "pciefabric/bandwidth.hpp"
#include "pciefabric/values.hpp"
#include "pciefabric/enums.hpp"
#include "pciefabric/provenance.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

static pci::Path make(const pci::PathId& id, pci::LocalityClass loc, pci::PcieGen gen,
                      std::uint16_t lanes, double gib, pci::ContentionState cont) {
  pci::Path p(id, pci::PathGeneration(1));
  p.set_endpoints(pci::PciNodeId(90), pci::PciNodeId(91));
  p.set_path_class(pci::PathClass::PEER_ENDPOINT_PATH);
  p.set_locality(loc);
  p.set_theoretical(pci::LinkEnvelope(gen, pci::LaneCount(lanes)));
  p.set_best_measured(pci::BytesPerSecond(gib * 1024.0 * 1024.0 * 1024.0), pci::Provenance::MEASURED);
  p.set_freshness(pci::FreshnessRecord(pci::Freshness::CURRENT, 0));
  p.set_contention(cont);
  p.set_contention_score(pci::ContentionScore(0.5));
  p.set_confidence(pci::Confidence(0.9));
  p.set_authority(pci::CoordinatorEpoch(7), pci::WorkerBootId(3), pci::HostGeneration(1),
                  pci::PathGeneration(1), pci::DeviceGeneration(1));
  p.add_capability(pci::Capability::PEER_ACCESS);
  return p;
}

int main(int argc, char** argv) {
  std::vector<pci::Path> cand;
  cand.push_back(make(pci::PathId(1), pci::LocalityClass::SAME_SWITCH, pci::PcieGen::GEN5, 16, 55.0, pci::ContentionState::IDLE));
  cand.push_back(make(pci::PathId(2), pci::LocalityClass::CROSS_NUMA, pci::PcieGen::GEN4, 16, 25.0, pci::ContentionState::MODERATE));
  cand.push_back(make(pci::PathId(3), pci::LocalityClass::SAME_ROOT_COMPLEX, pci::PcieGen::GEN4, 8, 12.0, pci::ContentionState::HIGH));
  cand.push_back(make(pci::PathId(4), pci::LocalityClass::SAME_SWITCH, pci::PcieGen::GEN3, 8, 7.0, pci::ContentionState::LOW));

  pci::SelectionRequest req;
  req.source = pci::PciNodeId(90); req.destination = pci::PciNodeId(91);
  req.epoch = pci::CoordinatorEpoch(7); req.boot = pci::WorkerBootId(3);
  req.required_capabilities = {pci::Capability::PEER_ACCESS};
  pci::PathSelector sel;

  std::uint64_t n = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 100000;
  if (n == 0) n = 1;
  if (n > 2000000) n = 2000000;

  std::uint64_t sink = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < n; ++i) {
    auto d = sel.select(cand, req);
    sink += d.selected ? d.selected->value() : 0u;
  }
  auto t1 = std::chrono::steady_clock::now();
  double sec = std::chrono::duration<double>(t1 - t0).count();
  if (sec <= 0.0) sec = 1e-9;
  std::printf("path ranking/select : %llu selects in %.3f s -> %.0f selects/s (sink=%llu)\n",
              static_cast<unsigned long long>(n), sec, static_cast<double>(n) / sec,
              static_cast<unsigned long long>(sink));
  return 0;
}
