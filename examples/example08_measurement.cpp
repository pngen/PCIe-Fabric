// SPDX-License-Identifier: Apache-2.0
// Example 08: Ingest a completed-transfer measurement and read it back, then
// demonstrate that a stale (non-newer) generation is rejected.
#include "pciefabric/measurement.hpp"
#include "pciefabric/values.hpp"
#include "pciefabric/enums.hpp"
#include <cstdio>

int main() {
  pci::MeasurementStore store;

  pci::Measurement m(pci::MeasurementId(1), pci::MeasurementGeneration(1),
                     pci::PciNodeId(90), pci::PciNodeId(91),
                     pci::TransferClass::PINNED_H2D, pci::EngineKind::SYNTHETIC,
                     pci::TransferBytes(1ull << 30), pci::IterationCount(4),
                     pci::Nanoseconds(1000000), pci::Provenance::MEASURED);
  m.set_path(pci::PathId(7), pci::PathGeneration(1));
  m.set_sync_semantics("cudaSync");
  m.set_warmup_ops(8);
  m.set_backend_detail("synthetic");

  auto r = store.ingest(m);
  std::printf("ingest #1 ok=%d\n", static_cast<int>(r.ok()));

  // Read the latest measurement for the (src,dst,class) tuple.
  auto latest = store.latest(pci::PciNodeId(90), pci::PciNodeId(91), pci::TransferClass::PINNED_H2D);
  if (latest) {
    std::printf("latest: bytes=%llu throughput=%.3f GiB/s latency=%llu ns prov=%s\n",
                static_cast<unsigned long long>(latest->bytes().value()),
                latest->throughput_gib().value(),
                static_cast<unsigned long long>(latest->latency_ns().value()),
                pci::to_string(latest->provenance()).c_str());
    std::printf("count_for_path(7)=%zu\n", store.count_for_path(pci::PathId(7)));
  }

  // Same generation must be rejected as stale.
  pci::Measurement same_gen(pci::MeasurementId(1), pci::MeasurementGeneration(1),
                            pci::PciNodeId(90), pci::PciNodeId(91),
                            pci::TransferClass::PINNED_H2D, pci::EngineKind::SYNTHETIC,
                            pci::TransferBytes(2ull << 30), pci::IterationCount(4),
                            pci::Nanoseconds(900000), pci::Provenance::MEASURED);
  auto r2 = store.ingest(same_gen);
  std::printf("ingest same generation ok=%d code=%d msg=%s\n",
              static_cast<int>(r2.ok()), static_cast<int>(r2.code()), r2.message().c_str());

  // A strictly newer generation is accepted.
  pci::Measurement newer(pci::MeasurementId(1), pci::MeasurementGeneration(2),
                         pci::PciNodeId(90), pci::PciNodeId(91),
                         pci::TransferClass::PINNED_H2D, pci::EngineKind::SYNTHETIC,
                         pci::TransferBytes(2ull << 30), pci::IterationCount(8),
                         pci::Nanoseconds(500000), pci::Provenance::MEASURED);
  auto r3 = store.ingest(newer);
  std::printf("ingest newer generation ok=%d\n", static_cast<int>(r3.ok()));
  auto latest2 = store.latest(pci::PciNodeId(90), pci::PciNodeId(91), pci::TransferClass::PINNED_H2D);
  if (latest2) {
    std::printf("latest after newer: gen=%llu throughput=%.3f GiB/s\n",
                static_cast<unsigned long long>(latest2->generation().value()),
                latest2->throughput_gib().value());
  }
  std::printf("store size=%zu\n", store.size());
  return 0;
}
