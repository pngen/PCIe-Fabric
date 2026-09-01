// SPDX-License-Identifier: Apache-2.0
// Example 12: Simulate cross-root placement: an accelerator on one root complex
// and a NIC on a second root complex. They share no upstream bridge, so locality
// is UNKNOWN (cross-root/remote) and there is no single shared bridge chain.
#include "pciefabric/topology.hpp"
#include "pciefabric/enums.hpp"
#include <cstdio>
#include <vector>

int main() {
  pci::Topology topo;
  auto rc1 = topo.add_root_complex(pci::RootComplexId(1), pci::Bdf(0, 0, 0, 0),
                                   pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC);
  auto sw1 = topo.add_bridge(pci::BridgeId(2), pci::Bdf(0, 1, 0, 0),
                             pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, rc1.value());
  auto acc = topo.add_endpoint(pci::PciNodeId(10), pci::Bdf(0, 2, 0, 0),
                               pci::DeviceClass::ACCELERATOR, pci::PciNodeGeneration(1),
                               pci::Provenance::SYNTHETIC, sw1.value());
  auto rc2 = topo.add_root_complex(pci::RootComplexId(3), pci::Bdf(0, 4, 0, 0),
                                   pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC);
  auto sw2 = topo.add_bridge(pci::BridgeId(4), pci::Bdf(0, 5, 0, 0),
                             pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, rc2.value());
  auto nic = topo.add_endpoint(pci::PciNodeId(11), pci::Bdf(0, 6, 0, 0),
                               pci::DeviceClass::NIC, pci::PciNodeGeneration(1),
                               pci::Provenance::SYNTHETIC, sw2.value());

  auto rca = topo.root_complex_of(acc.value());
  auto rcn = topo.root_complex_of(nic.value());
  std::printf("accelerator root complex: %s\n", rca ? topo.node(*rca)->bdf.canonical().c_str() : "-");
  std::printf("NIC root complex:         %s\n", rcn ? topo.node(*rcn)->bdf.canonical().c_str() : "-");

  // Cross-root: no shared common ancestor.
  auto ca = topo.common_ancestor(acc.value(), nic.value());
  std::printf("cross-root common ancestor: %s\n", ca ? topo.node(*ca)->bdf.canonical().c_str() : "(none)");
  std::printf("cross-root locality: %s\n", pci::to_string(topo.locality(acc.value(), nic.value())).c_str());

  auto path = topo.path_nodes(acc.value(), nic.value());
  std::printf("cross-root path_nodes (%zu nodes):", path.size());
  for (auto nid : path) {
    const pci::Node* n = topo.node(nid);
    if (n) std::printf(" %s", n->bdf.canonical().c_str());
  }
  std::printf("\n");

  // Contrast: within the same root complex, accelerator<->host is SAME_ROOT_COMPLEX.
  std::printf("accelerator<->host locality: %s\n", pci::to_string(topo.locality(acc.value(), rc1.value())).c_str());
  return 0;
}
