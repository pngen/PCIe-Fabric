// SPDX-License-Identifier: Apache-2.0
// Example 05: Resolve a host <-> accelerator path with structural facts, then
// attach the theoretical envelope and authority fencing.
#include "pciefabric/topology.hpp"
#include "pciefabric/path.hpp"
#include "pciefabric/bandwidth.hpp"
#include "pciefabric/values.hpp"
#include "pciefabric/provenance.hpp"
#include <cstdio>

int main() {
  pci::Topology topo;
  auto rc = topo.add_root_complex(pci::RootComplexId(1), pci::Bdf(0, 0, 0, 0),
                                  pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC);
  auto sw = topo.add_bridge(pci::BridgeId(2), pci::Bdf(0, 1, 0, 0),
                            pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, rc.value());
  auto acc = topo.add_endpoint(pci::PciNodeId(90), pci::Bdf(0, 2, 0, 0),
                               pci::DeviceClass::ACCELERATOR, pci::PciNodeGeneration(1),
                               pci::Provenance::SYNTHETIC, sw.value());

  const pci::CoordinatorEpoch epoch(7);
  const pci::WorkerBootId boot(3);
  pci::Path p = pci::build_path(topo, pci::PathId(1), pci::PathGeneration(1),
                                acc.value(), rc.value(), epoch, boot);

  std::printf("path id=%llu class=%s locality=%s hops=%u src=%llu dst=%llu\n",
              static_cast<unsigned long long>(p.id().value()),
              pci::to_string(p.path_class()).c_str(),
              pci::to_string(p.locality()).c_str(),
              static_cast<unsigned>(p.hop_count()),
              static_cast<unsigned long long>(p.source().value()),
              static_cast<unsigned long long>(p.destination().value()));

  std::printf("bridge chain:");
  for (auto nid : p.bridge_chain()) {
    const pci::Node* n = topo.node(nid);
    if (n) std::printf(" %s", n->bdf.canonical().c_str());
  }
  std::printf("\n");

  // Attach the DERIVED theoretical envelope and mark CURRENT.
  p.set_theoretical(pci::LinkEnvelope(pci::PcieGen::GEN5, pci::LaneCount(16)));
  p.set_freshness(pci::FreshnessRecord(pci::Freshness::CURRENT, 0));
  p.set_capabilities({pci::Capability::PEER_ACCESS, pci::Capability::GPUDIRECT});
  std::printf("theoretical=%.3f GiB/s raw=%.3e B/s has_GPUDIRECT=%d\n",
              p.theoretical()->effective_gib_per_sec().value(),
              p.theoretical()->raw_bytes_per_sec().value(),
              static_cast<int>(p.has_capability(pci::Capability::GPUDIRECT)));

  std::printf("authority matches (epoch=7,boot=3)=%d  (epoch=8,boot=3)=%d\n",
              static_cast<int>(p.authority_matches(pci::CoordinatorEpoch(7), pci::WorkerBootId(3))),
              static_cast<int>(p.authority_matches(pci::CoordinatorEpoch(8), pci::WorkerBootId(3))));
  return 0;
}
