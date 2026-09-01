// SPDX-License-Identifier: Apache-2.0
// Example 01: Discover the live PCI inventory on this Windows host and build
// a Topology from the real SetupAPI/Configuration Manager data.
#include "pciefabric/windows_enum.hpp"
#include <cstdio>

int main() {
  auto devs = pci::enumerate_pci_devices();
  if (!devs) {
    std::printf("enumerate_pci_devices() failed: %s (code=%d)\n",
                devs.message().c_str(), static_cast<int>(devs.code()));
    return 1;
  }
  const auto& list = devs.value();
  std::printf("discovered %zu PCI device(s)\n", list.size());
  for (const auto& d : list) {
    std::printf("  bdf=%-18s kind=%-10s vendor=%-6s device=%-6s class=%s bridge=%d parent=%s\n",
                d.bdf.canonical().c_str(),
                pci::to_string(pci::classify_pci_class(d.class_code)).c_str(),
                d.vendor_id.c_str(), d.device_id.c_str(), d.class_code.c_str(),
                static_cast<int>(d.is_bridge),
                d.parent_bdf ? d.parent_bdf->canonical().c_str() : "-");
  }

  auto topo = pci::build_topology_from_windows();
  if (!topo) {
    std::printf("build_topology_from_windows() failed: %s (code=%d)\n",
                topo.message().c_str(), static_cast<int>(topo.code()));
    return 1;
  }
  const auto& t = topo.value();
  std::printf("topology: %zu node(s), %zu endpoint(s)\n", t.size(), t.endpoints().size());
  return 0;
}
