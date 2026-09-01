// SPDX-License-Identifier: Apache-2.0
// Example 09: Reserve and release path capacity with fenced accounting, and
// demonstrate overcommit rejection and double-release rejection.
#include "pciefabric/reservation.hpp"
#include "pciefabric/values.hpp"
#include <cstdio>

int main() {
  pci::ReservationLimits limits{pci::CapacityBytes(8ull << 30), pci::Milliseconds(60000)};
  pci::ReservationRegistry reg(limits);

  pci::Reservation req(pci::ReservationId(1), pci::ReservationGeneration(1));
  req.set_path(pci::PathId(1), pci::PathGeneration(1));
  req.set_authority(pci::WorkerId(9), pci::WorkerBootId(2), pci::AttemptId(1),
                    pci::CoordinatorEpoch(5), pci::PolicyId(3), pci::PolicyGeneration(1));
  req.set_request(pci::CapacityBytes(1ull << 30), pci::BytesPerSecond(3.0e9),
                  pci::Milliseconds(5000), 5, pci::PciNodeId(90), pci::PciNodeId(91), "synthetic");

  auto r = reg.reserve(req);
  std::printf("reserve #1 ok=%d active_count=%zu total_count=%zu\n",
              static_cast<int>(r.ok()), reg.active_count(), reg.total_count());
  std::printf("reserved_bytes(path1)=%llu\n",
              static_cast<unsigned long long>(reg.reserved_bytes(pci::PathId(1))));

  // A second reservation on the same path must not exceed the per-path ceiling.
  pci::Reservation req2(pci::ReservationId(2), pci::ReservationGeneration(1));
  req2.set_path(pci::PathId(1), pci::PathGeneration(1));
  req2.set_authority(pci::WorkerId(9), pci::WorkerBootId(2), pci::AttemptId(1),
                     pci::CoordinatorEpoch(5), pci::PolicyId(3), pci::PolicyGeneration(1));
  req2.set_request(pci::CapacityBytes(8ull << 30), pci::BytesPerSecond(3.0e9),
                   pci::Milliseconds(5000), 5, pci::PciNodeId(90), pci::PciNodeId(91), "synthetic");
  auto r2 = reg.reserve(req2);
  std::printf("reserve #2 (would overcommit) ok=%d code=%d msg=%s\n",
              static_cast<int>(r2.ok()), static_cast<int>(r2.code()), r2.message().c_str());

  auto acc = reg.validate_accounting();
  std::printf("validate_accounting ok=%d\n", static_cast<int>(acc.ok()));

  auto rel = reg.release(pci::ReservationId(1), pci::WorkerBootId(2),
                         pci::ReservationGeneration(1), pci::CoordinatorEpoch(5));
  std::printf("release #1 ok=%d reserved_bytes(path1)=%llu\n",
              static_cast<int>(rel.ok()),
              static_cast<unsigned long long>(reg.reserved_bytes(pci::PathId(1))));

  auto rel2 = reg.release(pci::ReservationId(1), pci::WorkerBootId(2),
                          pci::ReservationGeneration(1), pci::CoordinatorEpoch(5));
  std::printf("release #2 (double) ok=%d code=%d msg=%s\n",
              static_cast<int>(rel2.ok()), static_cast<int>(rel2.code()), rel2.message().c_str());

  auto acc2 = reg.validate_accounting();
  std::printf("validate_accounting after release ok=%d active_count=%zu\n",
              static_cast<int>(acc2.ok()), reg.active_count());
  return 0;
}
