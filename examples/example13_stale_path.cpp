// SPDX-License-Identifier: Apache-2.0
// Example 13: Show stale path-generation (authority) rejection. A path's
// coordinator epoch / worker boot are fenced to the authority that built it; a
// request from a newer authority cannot select a stale path.
#include "pciefabric/path.hpp"
#include "pciefabric/selection.hpp"
#include "pciefabric/bandwidth.hpp"
#include "pciefabric/values.hpp"
#include "pciefabric/enums.hpp"
#include "pciefabric/provenance.hpp"
#include <cstdio>
#include <vector>

static pci::Path make(const pci::PathId& id, pci::CoordinatorEpoch epoch,
                      pci::Freshness fresh, pci::ContentionState cont) {
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

int main() {
  std::vector<pci::Path> cand;
  cand.push_back(make(pci::PathId(1), pci::CoordinatorEpoch(7), pci::Freshness::CURRENT, pci::ContentionState::IDLE));
  cand.push_back(make(pci::PathId(2), pci::CoordinatorEpoch(7), pci::Freshness::STALE, pci::ContentionState::IDLE));
  cand.push_back(make(pci::PathId(3), pci::CoordinatorEpoch(6), pci::Freshness::CURRENT, pci::ContentionState::IDLE));

  pci::PathSelector sel;

  pci::SelectionRequest cur;
  cur.source = pci::PciNodeId(90); cur.destination = pci::PciNodeId(91);
  cur.epoch = pci::CoordinatorEpoch(7); cur.boot = pci::WorkerBootId(3);
  cur.required_capabilities = {pci::Capability::PEER_ACCESS};
  auto dCur = sel.select(cand, cur);
  std::printf("request (epoch=7): decision=%s selected=%llu binding=[%s]\n",
              pci::to_string(dCur.decision).c_str(),
              dCur.selected ? static_cast<unsigned long long>(dCur.selected->value()) : 0u,
              dCur.binding_constraint.c_str());

  pci::SelectionRequest next;
  next.source = pci::PciNodeId(90); next.destination = pci::PciNodeId(91);
  next.epoch = pci::CoordinatorEpoch(8); next.boot = pci::WorkerBootId(3);
  next.required_capabilities = {pci::Capability::PEER_ACCESS};
  auto dNext = sel.select(cand, next);
  std::printf("request (epoch=8, authority advanced): decision=%s binding=[%s]\n",
              pci::to_string(dNext.decision).c_str(), dNext.binding_constraint.c_str());
  std::printf("  eliminated:");
  for (auto id : dNext.eliminated) std::printf(" path%llu", static_cast<unsigned long long>(id.value()));
  std::printf("  (reasons): ");
  for (const auto& r : dNext.reasons) std::printf("\"%s\" ", r.c_str());
  std::printf("\n");
  return 0;
}
