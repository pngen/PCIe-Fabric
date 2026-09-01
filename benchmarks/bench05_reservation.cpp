// SPDX-License-Identifier: Apache-2.0
// Benchmark 05: Reservation create/release accounting. Reports ops/s.
#include "pciefabric/reservation.hpp"
#include "pciefabric/values.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

int main(int argc, char** argv) {
  pci::ReservationLimits limits{pci::CapacityBytes(16ull << 30), pci::Milliseconds(60000)};
  pci::ReservationRegistry reg(limits);

  std::uint64_t n = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 50000;
  if (n == 0) n = 1;
  if (n > 500000) n = 500000;

  std::uint64_t ok = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < n; ++i) {
    pci::ReservationId rid = pci::ReservationId(i + 1);
    pci::Reservation req(rid, pci::ReservationGeneration(1));
    req.set_path(pci::PathId(1), pci::PathGeneration(1));
    req.set_authority(pci::WorkerId(1), pci::WorkerBootId(2), pci::AttemptId(1),
                      pci::CoordinatorEpoch(5), pci::PolicyId(3), pci::PolicyGeneration(1));
    req.set_request(pci::CapacityBytes(1ull << 30), pci::BytesPerSecond(3.0e9),
                    pci::Milliseconds(1000), 5, pci::PciNodeId(90), pci::PciNodeId(91), "bench");
    auto r = reg.reserve(req);
    if (r.ok()) {
      ++ok;
      auto rel = reg.release(rid, pci::WorkerBootId(2), pci::ReservationGeneration(1), pci::CoordinatorEpoch(5));
      if (rel.ok()) ++ok;
    }
  }
  auto t1 = std::chrono::steady_clock::now();
  double sec = std::chrono::duration<double>(t1 - t0).count();
  if (sec <= 0.0) sec = 1e-9;
  std::printf("reservation create/release: %llu pairs in %.3f s -> %.0f ops/s (ok=%llu)\n",
              static_cast<unsigned long long>(n), sec, static_cast<double>(n) / sec,
              static_cast<unsigned long long>(ok));
  return 0;
}
