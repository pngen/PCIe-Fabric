// SPDX-License-Identifier: Apache-2.0
// Example 11: Simulate a shared-switch topology and show how two endpoints that
// share a switch get the same structural contention domain (SAME_SWITCH), while
// endpoints on two different switches under one root complex are SAME_ROOT_COMPLEX.
#include "pciefabric/topology.hpp"
#include "pciefabric/enums.hpp"
#include <cstdio>

int main() {
  pci::Topology topo;
  auto rc = topo.add_root_complex(pci::RootComplexId(1), pci::Bdf(0, 0, 0, 0),
                                  pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC);
  auto swA = topo.add_bridge(pci::BridgeId(2), pci::Bdf(0, 1, 0, 0),
                             pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, rc.value());
  auto swB = topo.add_bridge(pci::BridgeId(3), pci::Bdf(0, 1, 1, 0),
                             pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, rc.value());
  auto epA = topo.add_endpoint(pci::PciNodeId(10), pci::Bdf(0, 2, 0, 0),
                               pci::DeviceClass::ACCELERATOR, pci::PciNodeGeneration(1),
                               pci::Provenance::SYNTHETIC, swA.value());
  auto epC = topo.add_endpoint(pci::PciNodeId(12), pci::Bdf(0, 2, 0, 1),
                               pci::DeviceClass::NIC, pci::PciNodeGeneration(1),
                               pci::Provenance::SYNTHETIC, swA.value());
  auto epB = topo.add_endpoint(pci::PciNodeId(11), pci::Bdf(0, 3, 0, 0),
                               pci::DeviceClass::STORAGE, pci::PciNodeGeneration(1),
                               pci::Provenance::SYNTHETIC, swB.value());

  auto shA = topo.shared_contention_node(epA.value(), epC.value());
  std::printf("endpoints A and C share switch: locality=%s shared=[%s]\n",
              pci::to_string(topo.locality(epA.value(), epC.value())).c_str(),
              shA ? topo.node(*shA)->bdf.canonical().c_str() : "-");

  auto shB = topo.shared_contention_node(epA.value(), epB.value());
  std::printf("endpoints A and B share root complex: locality=%s shared=[%s]\n",
              pci::to_string(topo.locality(epA.value(), epB.value())).c_str(),
              shB ? topo.node(*shB)->bdf.canonical().c_str() : "-");

  // A peer path between A (switch A) and B (switch B) must climb through the root complex.
  auto pathAB = topo.path_nodes(epA.value(), epB.value());
  std::printf("path A->B (%zu nodes):", pathAB.size());
  for (auto nid : pathAB) {
    const pci::Node* n = topo.node(nid);
    if (n) std::printf(" %s", n->bdf.canonical().c_str());
  }
  std::printf("\n");
  return 0;
}
