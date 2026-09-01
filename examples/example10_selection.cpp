// SPDX-License-Identifier: Apache-2.0
// Example 10: Explain path selection. The decision carries the binding
// constraint, a reasons list, eliminated paths and the ranked alternatives.
#include "pciefabric/path.hpp"
#include "pciefabric/selection.hpp"
#include "pciefabric/bandwidth.hpp"
#include "pciefabric/values.hpp"
#include "pciefabric/enums.hpp"
#include "pciefabric/provenance.hpp"
#include <cstdio>
#include <vector>

static pci::Path make(const pci::PathId& id, pci::ContentionState cont,
                      pci::Freshness fresh, pci::CoordinatorEpoch epoch) {
  pci::Path p(id, pci::PathGeneration(1));
  p.set_endpoints(pci::PciNodeId(90), pci::PciNodeId(91));
  p.set_path_class(pci::PathClass::PEER_ENDPOINT_PATH);
  p.set_locality(pci::LocalityClass::SAME_SWITCH);
  p.set_theoretical(pci::LinkEnvelope(pci::PcieGen::GEN5, pci::LaneCount(16)));
  p.set_best_measured(pci::BytesPerSecond(55.0 * 1024.0 * 1024.0 * 1024.0), pci::Provenance::MEASURED);
  p.set_freshness(pci::FreshnessRecord(fresh, 0));
  p.set_contention(cont);
  p.set_contention_score(pci::ContentionScore(0.5));
  p.set_confidence(pci::Confidence(0.9));
  p.set_authority(epoch, pci::WorkerBootId(3), pci::HostGeneration(1),
                  pci::PathGeneration(1), pci::DeviceGeneration(1));
  p.add_capability(pci::Capability::PEER_ACCESS);
  return p;
}

static void print_decision(const char* label, const pci::SelectionDecision& d) {
  std::printf("%s: decision=%s selected=%llu binding=[%s]\n", label,
              pci::to_string(d.decision).c_str(),
              d.selected ? static_cast<unsigned long long>(d.selected->value()) : 0u,
              d.binding_constraint.c_str());
  std::printf("  reasons:");
  for (const auto& r : d.reasons) std::printf(" [%s]", r.c_str());
  std::printf("\n  alternatives:");
  for (auto id : d.alternatives) std::printf(" path%llu", static_cast<unsigned long long>(id.value()));
  std::printf("\n  eliminated:");
  for (auto id : d.eliminated) std::printf(" path%llu", static_cast<unsigned long long>(id.value()));
  std::printf("\n  ranked:");
  for (auto id : d.reranked) std::printf(" path%llu", static_cast<unsigned long long>(id.value()));
  std::printf("\n");
}

int main() {
  pci::PathSelector sel;
  pci::SelectionRequest req;
  req.source = pci::PciNodeId(90); req.destination = pci::PciNodeId(91);
  req.epoch = pci::CoordinatorEpoch(7); req.boot = pci::WorkerBootId(3);
  req.required_capabilities = {pci::Capability::PEER_ACCESS};

  // Scenario A: a mix that includes a saturated path (deferred), a stale path
  // (eliminated) and a good path.
  std::vector<pci::Path> candA;
  candA.push_back(make(pci::PathId(1), pci::ContentionState::IDLE, pci::Freshness::CURRENT, pci::CoordinatorEpoch(7)));
  candA.push_back(make(pci::PathId(2), pci::ContentionState::SATURATED, pci::Freshness::CURRENT, pci::CoordinatorEpoch(7)));
  candA.push_back(make(pci::PathId(3), pci::ContentionState::IDLE, pci::Freshness::STALE, pci::CoordinatorEpoch(7)));
  auto dA = sel.select(candA, req);
  print_decision("scenario A", dA);

  // Scenario B: all paths are stale authority (epoch mismatch) -> REJECT.
  std::vector<pci::Path> candB;
  candB.push_back(make(pci::PathId(1), pci::ContentionState::IDLE, pci::Freshness::CURRENT, pci::CoordinatorEpoch(6)));
  candB.push_back(make(pci::PathId(2), pci::ContentionState::IDLE, pci::Freshness::CURRENT, pci::CoordinatorEpoch(6)));
  auto dB = sel.select(candB, req);
  print_decision("scenario B", dB);
  return 0;
}
