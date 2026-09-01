// SPDX-License-Identifier: Apache-2.0
// Benchmark 06: Persistence save/load. Reports ops/s and bytes/s.
#include "pciefabric/persistence.hpp"
#include "pciefabric/snapshot.hpp"
#include "pciefabric/path.hpp"
#include "pciefabric/measurement.hpp"
#include "pciefabric/values.hpp"
#include "pciefabric/provenance.hpp"
#include "pciefabric/topology.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <string>

int main(int argc, char** argv) {
  const std::string file = (std::filesystem::temp_directory_path() / "pf_bench_persist.bin").string();

  // Build a small set of path + measurement records to serialize.
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

  pci::Snapshot snap;
  snap.epoch = pci::CoordinatorEpoch(1);
  for (std::uint64_t i = 0; i < 8; ++i) {
    pci::Path p = pci::build_path(topo, pci::PathId(i + 1), pci::PathGeneration(1),
                                  a.value(), b.value(), pci::CoordinatorEpoch(1), pci::WorkerBootId(1));
    p.set_theoretical(pci::LinkEnvelope(pci::PcieGen::GEN5, pci::LaneCount(16)));
    p.set_freshness(pci::FreshnessRecord(pci::Freshness::CURRENT, 0));
    snap.paths.push_back(p);
    pci::Measurement m(pci::MeasurementId(i + 1), pci::MeasurementGeneration(1), a.value(), b.value(),
                       pci::TransferClass::PINNED_H2D, pci::EngineKind::SYNTHETIC,
                       pci::TransferBytes(1ull << 30), pci::IterationCount(4),
                       pci::Nanoseconds(1000000), pci::Provenance::MEASURED);
    m.set_path(pci::PathId(i + 1), pci::PathGeneration(1));
    snap.measurements.push_back(m);
  }
  snap.semantic_digest = pci::semantic_digest(snap);

  std::uint64_t n = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 200;
  if (n == 0) n = 1;
  if (n > 2000) n = 2000;

  std::uint64_t ok = 0;
  std::uint64_t bytes_total = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < n; ++i) {
    auto s = pci::persist::save(file, snap);
    if (s.ok()) {
      auto l = pci::persist::load(file);
      if (l.ok()) ++ok;
      bytes_total += static_cast<std::uint64_t>(std::filesystem::file_size(file));
    }
  }
  auto t1 = std::chrono::steady_clock::now();
  double sec = std::chrono::duration<double>(t1 - t0).count();
  if (sec <= 0.0) sec = 1e-9;
  std::printf("persistence save/load: %llu round-trips in %.3f s -> %.0f ops/s, %.1f MiB/s\n",
              static_cast<unsigned long long>(n), sec, static_cast<double>(n) / sec,
              (static_cast<double>(bytes_total) / sec) / (1024.0 * 1024.0));
  std::filesystem::remove(file);
  return 0;
}
