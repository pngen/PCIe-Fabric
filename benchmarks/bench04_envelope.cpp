// SPDX-License-Identifier: Apache-2.0
// Benchmark 04: Theoretical PCIe envelope derivation. Reports ops/s and bytes/s.
#include "pciefabric/bandwidth.hpp"
#include "pciefabric/values.hpp"
#include "pciefabric/enums.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

int main(int argc, char** argv) {
  std::uint64_t n = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 500000;
  if (n == 0) n = 1;
  if (n > 5000000) n = 5000000;
  const pci::PcieGen gen = pci::PcieGen::GEN5;
  const std::uint16_t lanes = 16;

  double sink = 0.0;
  auto t0 = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < n; ++i) {
    auto bps = pci::envelope::effective_bytes_per_sec(gen, lanes);
    pci::LinkEnvelope env(gen, pci::LaneCount(lanes));
    sink += bps.value() + env.effective_bytes_per_sec().value();
  }
  auto t1 = std::chrono::steady_clock::now();
  double sec = std::chrono::duration<double>(t1 - t0).count();
  if (sec <= 0.0) sec = 1e-9;
  std::printf("theoretical envelope: %llu derivations in %.3f s -> %.0f ops/s, %.3f GiB/s aggregated\n",
              static_cast<unsigned long long>(n), sec, static_cast<double>(n) / sec,
              sink / (static_cast<double>(n) * 2.0 * 1024.0 * 1024.0 * 1024.0));
  return 0;
}
