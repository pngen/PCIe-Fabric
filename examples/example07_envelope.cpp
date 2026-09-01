// SPDX-License-Identifier: Apache-2.0
// Example 07: Calculate the theoretical PCIe link envelope across generations
// and lane widths. All values are DERIVED (never measured).
#include "pciefabric/bandwidth.hpp"
#include "pciefabric/values.hpp"
#include "pciefabric/enums.hpp"
#include <cstdio>

int main() {
  const pci::PcieGen gens[] = {pci::PcieGen::GEN1, pci::PcieGen::GEN2, pci::PcieGen::GEN3,
                               pci::PcieGen::GEN4, pci::PcieGen::GEN5, pci::PcieGen::GEN6};
  const unsigned lanes[] = {1, 4, 8, 16};
  std::printf("%-6s %-4s %-12s %-12s %-12s\n", "gen", "x", "GiB/s", "B/s", "raw B/s");
  for (auto g : gens) {
    for (unsigned l : lanes) {
      if (!pci::envelope::lane_count_valid(static_cast<std::uint16_t>(l))) continue;
      auto gib = pci::envelope::effective_gib_per_sec(g, static_cast<std::uint16_t>(l));
      auto bps = pci::envelope::effective_bytes_per_sec(g, static_cast<std::uint16_t>(l));
      auto raw = pci::envelope::raw_bytes_per_sec(g, static_cast<std::uint16_t>(l));
      std::printf("%-6s %-4u %-12.3f %-12.6e %-12.6e\n", pci::to_string(g).c_str(), l,
                  gib.value(), bps.value(), raw);
    }
  }

  // LinkEnvelope wraps a full envelope with provenance = DERIVED by construction.
  pci::LinkEnvelope env(pci::PcieGen::GEN4, pci::LaneCount(16));
  std::printf("LinkEnvelope(GEN4 x16): valid=%d eff=%.3f GiB/s raw=%.3e B/s prov=%s\n",
              static_cast<int>(env.valid()), env.effective_gib_per_sec().value(),
              env.raw_bytes_per_sec().value(), pci::to_string(env.provenance()).c_str());

  std::printf("encoding ratio GEN1 (8b/10b)=%.4f  GEN5 (128b/130b)=%.4f\n",
              pci::envelope::encoding_ratio(pci::PcieGen::GEN1),
              pci::envelope::encoding_ratio(pci::PcieGen::GEN5));
  return 0;
}
