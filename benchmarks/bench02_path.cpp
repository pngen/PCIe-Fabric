// SPDX-License-Identifier: Apache-2.0
// Benchmark 02: Path construction from a fixed topology. Reports paths/s.
#include "pciefabric/topology.hpp"
#include "pciefabric/path.hpp"
#include "pciefabric/values.hpp"
#include "pciefabric/provenance.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

int main(int argc, char** argv) {
  pci::Topology topo;
  auto rc = topo.add_root_complex(pci::RootComplexId(1), pci::Bdf(0, 0, 0, 0),
                                  pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC);
  auto sw = topo.add_bridge(pci::BridgeId(2), pci::Bdf(0, 1, 0, 0),
                            pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, rc.value());
  auto a = topo.add_endpoint(pci::PciNodeId(90), pci::Bdf(0, 2, 0, 0),
                             pci::DeviceClass::ACCELERATOR, pci::PciNodeGeneration(1),
                             pci::Provenance::SYNTHETIC, sw.value());
  auto b = topo.add_endpoint(pci::PciNodeId(91), pci::Bdf(0, 2, 0, 1),
                             pci::DeviceClass::NIC, pci::PciNodeGeneration(1),
                             pci::Provenance::SYNTHETIC, sw.value());

  std::uint64_t n = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 20000;
  if (n == 0) n = 1;
  if (n > 500000) n = 500000;

  std::uint64_t sink = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < n; ++i) {
    pci::Path p = pci::build_path(topo, pci::PathId((i % 1000u) + 1u), pci::PathGeneration(1),
                                  a.value(), b.value(), pci::CoordinatorEpoch(7), pci::WorkerBootId(3));
    sink += p.hop_count();
  }
  auto t1 = std::chrono::steady_clock::now();
  double sec = std::chrono::duration<double>(t1 - t0).count();
  if (sec <= 0.0) sec = 1e-9;
  std::printf("path construction   : %llu paths in %.3f s -> %.0f paths/s (sink=%llu)\n",
              static_cast<unsigned long long>(n), sec, static_cast<double>(n) / sec,
              static_cast<unsigned long long>(sink));
  return 0;
}
