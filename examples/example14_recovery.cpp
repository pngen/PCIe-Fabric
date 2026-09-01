// SPDX-License-Identifier: Apache-2.0
// Example 14: Persistence and recovery. Build a real path + measurement records
// from an in-memory hierarchy, serialize them with the deterministic versioned
// persistence layer, load them back, and confirm the recovered physical
// observations are downgraded to REVALIDATION_REQUIRED before re-entering use.
#include "pciefabric/topology.hpp"
#include "pciefabric/path.hpp"
#include "pciefabric/persistence.hpp"
#include "pciefabric/snapshot.hpp"
#include "pciefabric/bandwidth.hpp"
#include "pciefabric/values.hpp"
#include "pciefabric/provenance.hpp"
#include "pciefabric/measurement.hpp"
#include <cstdio>
#include <filesystem>
#include <string>

int main() {
  const std::string file = "pf_ex_recovery.bin";

  // Build an in-memory hierarchy purely to derive a real path record.
  pci::Topology topo;
  auto rc = topo.add_root_complex(pci::RootComplexId(1), pci::Bdf(0, 0, 0, 0),
                                  pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC);
  auto sw = topo.add_bridge(pci::BridgeId(2), pci::Bdf(0, 1, 0, 0),
                            pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, rc.value());
  auto acc = topo.add_endpoint(pci::PciNodeId(90), pci::Bdf(0, 2, 0, 0),
                               pci::DeviceClass::ACCELERATOR, pci::PciNodeGeneration(1),
                               pci::Provenance::SYNTHETIC, sw.value());
  auto nic = topo.add_endpoint(pci::PciNodeId(91), pci::Bdf(0, 2, 0, 1),
                               pci::DeviceClass::NIC, pci::PciNodeGeneration(1),
                               pci::Provenance::SYNTHETIC, sw.value());

  pci::Path p = pci::build_path(topo, pci::PathId(1), pci::PathGeneration(1),
                                acc.value(), nic.value(), pci::CoordinatorEpoch(1), pci::WorkerBootId(1));
  p.set_theoretical(pci::LinkEnvelope(pci::PcieGen::GEN5, pci::LaneCount(16)));
  p.set_freshness(pci::FreshnessRecord(pci::Freshness::CURRENT, 0));
  p.set_best_measured(pci::BytesPerSecond(55.0 * 1024.0 * 1024.0 * 1024.0), pci::Provenance::MEASURED);
  p.set_confidence(pci::Confidence(0.9));

  pci::Measurement m(pci::MeasurementId(1), pci::MeasurementGeneration(1),
                     acc.value(), nic.value(), pci::TransferClass::PINNED_H2D,
                     pci::EngineKind::SYNTHETIC, pci::TransferBytes(1ull << 30),
                     pci::IterationCount(4), pci::Nanoseconds(1000000), pci::Provenance::MEASURED);
  m.set_path(pci::PathId(1), pci::PathGeneration(1));

  pci::Snapshot snap;
  snap.epoch = pci::CoordinatorEpoch(1);
  snap.paths.push_back(p);
  snap.measurements.push_back(m);
  snap.semantic_digest = pci::semantic_digest(snap);
  std::printf("pre-save: paths=%zu measurements=%zu digest=%llu\n",
              snap.paths.size(), snap.measurements.size(),
              static_cast<unsigned long long>(snap.semantic_digest));

  auto saved = pci::persist::save(file, snap);
  std::printf("persist::save ok=%d (%s) file_bytes=%llu\n",
              static_cast<int>(saved.ok()), saved.message().c_str(),
              static_cast<unsigned long long>(std::filesystem::file_size(file)));

  auto loaded = pci::persist::load(file);
  if (!loaded) {
    std::printf("persist::load failed: %s (code=%d)\n", loaded.message().c_str(),
                static_cast<int>(loaded.code()));
    std::filesystem::remove(file);
    return 1;
  }
  pci::Snapshot rec = loaded.value();
  std::printf("post-load: paths=%zu measurements=%zu digest=%llu\n",
              rec.paths.size(), rec.measurements.size(),
              static_cast<unsigned long long>(rec.semantic_digest));
  std::printf("digest_preserved=%d\n",
              static_cast<int>(rec.semantic_digest == snap.semantic_digest));

  // Recovery is never implicitly CURRENT: physical observations are downgraded.
  pci::persist::mark_physical_revalidation_required(rec);
  bool any_current = false, any_reval = false;
  for (const auto& pp : rec.paths) {
    if (pp.freshness().state() == pci::Freshness::CURRENT) any_current = true;
    if (pp.freshness().state() == pci::Freshness::REVALIDATION_REQUIRED) any_reval = true;
  }
  std::printf("recovered freshness: any_CURRENT=%d any_REVALIDATION_REQUIRED=%d\n",
              static_cast<int>(any_current), static_cast<int>(any_reval));

  std::filesystem::remove(file);
  return 0;
}
