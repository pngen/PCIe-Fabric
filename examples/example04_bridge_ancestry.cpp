// SPDX-License-Identifier: Apache-2.0
// Example 04: Show bridge ancestry in a multi-level PCIe hierarchy and how a
// path climbs through bridges to a common ancestor.
#include "pciefabric/topology.hpp"
#include "pciefabric/enums.hpp"
#include <cstdio>
#include <vector>

int main() {
  pci::Topology topo;
  // root complex -> bridge L1 -> bridge L2 -> two endpoints.
  auto rc = topo.add_root_complex(pci::RootComplexId(1), pci::Bdf(0, 0, 0, 0),
                                  pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC);
  auto b1 = topo.add_bridge(pci::BridgeId(2), pci::Bdf(0, 1, 0, 0),
                            pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, rc.value());
  auto b2 = topo.add_bridge(pci::BridgeId(3), pci::Bdf(0, 1, 1, 0),
                            pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, b1.value());
  auto epA = topo.add_endpoint(pci::PciNodeId(50), pci::Bdf(0, 2, 0, 0),
                               pci::DeviceClass::ACCELERATOR, pci::PciNodeGeneration(1),
                               pci::Provenance::SYNTHETIC, b2.value());
  auto epB = topo.add_endpoint(pci::PciNodeId(51), pci::Bdf(0, 2, 0, 1),
                               pci::DeviceClass::NIC, pci::PciNodeGeneration(1),
                               pci::Provenance::SYNTHETIC, b2.value());

  auto ancA = topo.ancestors(epA.value());
  std::printf("ancestry of endpoint A (%zu nodes, including self):\n", ancA.size());
  for (auto a : ancA) {
    const pci::Node* n = topo.node(a);
    if (n) std::printf("  %-18s kind=%-12s\n", n->bdf.canonical().c_str(), pci::to_string(n->kind).c_str());
  }

  auto ca = topo.common_ancestor(epA.value(), epB.value());
  if (ca) {
    std::printf("common ancestor of A and B: %s (kind=%s)\n",
                topo.node(*ca)->bdf.canonical().c_str(),
                pci::to_string(topo.node(*ca)->kind).c_str());
  }

  auto path = topo.path_nodes(epA.value(), epB.value());
  std::printf("path_nodes A->B (%zu nodes):", path.size());
  for (auto nid : path) {
    const pci::Node* n = topo.node(nid);
    if (n) std::printf(" %s", n->bdf.canonical().c_str());
  }
  std::printf("\n");

  std::printf("locality(A,B)=%s\n", pci::to_string(topo.locality(epA.value(), epB.value())).c_str());
  return 0;
}
