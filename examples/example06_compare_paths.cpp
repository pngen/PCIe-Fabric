// SPDX-License-Identifier: Apache-2.0
// Example 06: Compare candidate paths component-wise and rank them. Ranking is
// not a single opaque score: locality, measured throughput, theoretical
// envelope, contention and confidence are compared in order.
#include "pciefabric/path.hpp"
#include "pciefabric/selection.hpp"
#include "pciefabric/bandwidth.hpp"
#include "pciefabric/values.hpp"
#include "pciefabric/enums.hpp"
#include "pciefabric/provenance.hpp"
#include <cstdio>
#include <vector>

static pci::Path make_path(const pci::PathId& id, const pci::PciNodeId& src,
                           const pci::PciNodeId& dst, pci::LocalityClass loc,
                           pci::PcieGen gen, std::uint16_t lanes,
                           double measured_gib, pci::ContentionState cont,
                           pci::Confidence conf) {
  pci::Path p(id, pci::PathGeneration(1));
  p.set_endpoints(src, dst);
  p.set_path_class(pci::PathClass::PEER_ENDPOINT_PATH);
  p.set_locality(loc);
  p.set_theoretical(pci::LinkEnvelope(gen, pci::LaneCount(lanes)));
  p.set_best_measured(pci::BytesPerSecond(measured_gib * 1024.0 * 1024.0 * 1024.0), pci::Provenance::MEASURED);
  p.set_freshness(pci::FreshnessRecord(pci::Freshness::CURRENT, 0));
  p.set_contention(cont);
  p.set_contention_score(pci::ContentionScore(conf.value() * 0.3));
  p.set_confidence(conf);
  p.set_authority(pci::CoordinatorEpoch(7), pci::WorkerBootId(3), pci::HostGeneration(1),
                  pci::PathGeneration(1), pci::DeviceGeneration(1));
  p.add_capability(pci::Capability::PEER_ACCESS);
  return p;
}

int main() {
  const pci::PciNodeId src(90), dst(91);
  std::vector<pci::Path> cand;
  cand.push_back(make_path(pci::PathId(1), src, dst, pci::LocalityClass::SAME_SWITCH,
                           pci::PcieGen::GEN5, 16, 55.0, pci::ContentionState::IDLE, pci::Confidence(0.95)));
  cand.push_back(make_path(pci::PathId(2), src, dst, pci::LocalityClass::CROSS_NUMA,
                           pci::PcieGen::GEN4, 16, 25.0, pci::ContentionState::MODERATE, pci::Confidence(0.7)));
  cand.push_back(make_path(pci::PathId(3), src, dst, pci::LocalityClass::SAME_ROOT_COMPLEX,
                           pci::PcieGen::GEN4, 8, 12.0, pci::ContentionState::HIGH, pci::Confidence(0.6)));
  // A plain candidate with no measured evidence (falls back to theoretical).
  pci::Path p4(pci::PathId(4), pci::PathGeneration(1));
  p4.set_endpoints(src, dst);
  p4.set_path_class(pci::PathClass::PEER_ENDPOINT_PATH);
  p4.set_locality(pci::LocalityClass::SAME_SWITCH);
  p4.set_theoretical(pci::LinkEnvelope(pci::PcieGen::GEN3, pci::LaneCount(8)));
  p4.set_freshness(pci::FreshnessRecord(pci::Freshness::CURRENT, 0));
  p4.set_contention(pci::ContentionState::IDLE);
  p4.set_confidence(pci::Confidence(0.5));
  p4.set_authority(pci::CoordinatorEpoch(7), pci::WorkerBootId(3), pci::HostGeneration(1),
                   pci::PathGeneration(1), pci::DeviceGeneration(1));
  p4.add_capability(pci::Capability::PEER_ACCESS);
  cand.push_back(p4);

  pci::SelectionRequest req;
  req.source = src; req.destination = dst;
  req.epoch = pci::CoordinatorEpoch(7); req.boot = pci::WorkerBootId(3);
  req.required_capabilities = {pci::Capability::PEER_ACCESS};
  req.priority = 3;

  pci::PathSelector sel;
  auto ranked = sel.rank(cand, req);
  std::printf("ranked order:");
  for (auto id : ranked) std::printf(" path%llu", static_cast<unsigned long long>(id.value()));
  std::printf("\n");

  auto d = sel.select(cand, req);
  std::printf("decision=%s selected=%llu locality=%s measured=%.1f GiB/s theoretical=%.2f GiB/s\n",
              pci::to_string(d.decision).c_str(),
              d.selected ? static_cast<unsigned long long>(d.selected->value()) : 0u,
              pci::to_string(d.locality).c_str(),
              d.measured ? d.measured->value() / (1024.0 * 1024.0 * 1024.0) : 0.0,
              d.theoretical ? d.theoretical->effective_gib_per_sec().value() : 0.0);
  std::printf("alternatives:");
  for (auto id : d.alternatives) std::printf(" path%llu", static_cast<unsigned long long>(id.value()));
  std::printf("\nbinding_constraint: %s\n", d.binding_constraint.c_str());
  return 0;
}
