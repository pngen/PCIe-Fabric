// SPDX-License-Identifier: Apache-2.0
// Example 03: Resolve an accelerator endpoint. Builds a small synthetic
// hierarchy for determinism, then (when CUDA is available) correlates the real
// CUDA device with the resolved accelerator.
#include "pciefabric/topology.hpp"
#include "pciefabric/path.hpp"
#include "pciefabric/bandwidth.hpp"
#include "pciefabric/values.hpp"
#include "pciefabric/provenance.hpp"
#include <cstdio>

#ifdef PCIEFABRIC_HAVE_CUDA
#include "pciefabric/cuda_engine.hpp"
#endif

int main() {
  // Synthetic hierarchy: root complex -> switch -> { accelerator, NIC }.
  pci::Topology topo;
  auto rc = topo.add_root_complex(pci::RootComplexId(1), pci::Bdf(0, 0, 0, 0),
                                  pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC);
  auto sw = topo.add_bridge(pci::BridgeId(2), pci::Bdf(0, 1, 0, 0),
                            pci::PciNodeGeneration(1), pci::Provenance::SYNTHETIC, rc.value());
  auto gpu = topo.add_endpoint(pci::PciNodeId(100), pci::Bdf(0, 2, 0, 0),
                               pci::DeviceClass::ACCELERATOR, pci::PciNodeGeneration(1),
                               pci::Provenance::SYNTHETIC, sw.value());
  auto nic = topo.add_endpoint(pci::PciNodeId(101), pci::Bdf(0, 2, 0, 1),
                               pci::DeviceClass::NIC, pci::PciNodeGeneration(1),
                               pci::Provenance::SYNTHETIC, sw.value());

  std::printf("built topology: %zu node(s), %zu endpoint(s)\n", topo.size(), topo.endpoints().size());

  // Resolve every accelerator endpoint in the topology.
  std::printf("resolved accelerator node: %llu (bdf=%s kind=%s)\n",
              static_cast<unsigned long long>(gpu.value().value()),
              topo.node(gpu.value())->bdf.canonical().c_str(),
              pci::to_string(topo.node(gpu.value())->kind).c_str());
  auto rc_of_gpu = topo.root_complex_of(gpu.value());
  if (rc_of_gpu) {
    std::printf("  root complex of accelerator: node=%llu bdf=%s\n",
                static_cast<unsigned long long>(rc_of_gpu->value()),
                topo.node(*rc_of_gpu)->bdf.canonical().c_str());
  }

  // Theoretical envelope for a host <-> accelerator link (DERIVED).
  auto env = pci::envelope::effective_gib_per_sec(pci::PcieGen::GEN5, 16);
  std::printf("  accelerator theoretical envelope (GEN5 x16): %.3f GiB/s\n", env.value());

  // Correlate CUDA device, if available.
#ifdef PCIEFABRIC_HAVE_CUDA
  auto d = pci::cudap::detect_device();
  if (!d) {
    std::printf("CUDA unavailable: %s (code=%d)\n", d.message().c_str(), static_cast<int>(d.code()));
  } else {
    const pci::CudaDeviceInfo& di = d.value();
    std::printf("CUDA device present: '%s' ordinal=%d bdf=%s cc=%d.%d mem=%llu\n",
                di.name.c_str(), di.ordinal, di.bdf.canonical().c_str(),
                di.cc_major, di.cc_minor, static_cast<unsigned long long>(di.total_mem));
  }
#else
  std::printf("CUDA unavailable (library built without CUDA)\n");
#endif

  (void)nic;
  (void)env;
  return 0;
}
