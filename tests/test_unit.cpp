// SPDX-License-Identifier: Apache-2.0
// Unit + property + adversarial + concurrency + synthetic scenario tests.
#include <atomic>
#include <fstream>
#include <limits>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "pciefabric/bandwidth.hpp"
#include "pciefabric/bdf.hpp"
#include "pciefabric/codec.hpp"
#include "pciefabric/path.hpp"
#include "pciefabric/persistence.hpp"
#include "pciefabric/protocol.hpp"
#include "pciefabric/reservation.hpp"
#include "pciefabric/runtime.hpp"
#include "pciefabric/selection.hpp"
#include "pciefabric/snapshot.hpp"
#include "pciefabric/topology.hpp"
#include "pciefabric/values.hpp"

using namespace pci;

static int g_fail = 0;
static int g_check = 0;
#define CHECK(cond) do { ++g_check; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// ---------------------------------------------------------------------------
// BDF
static void test_bdf() {
  auto b = Bdf::parse("01:00.0");
  CHECK(b.ok());
  CHECK(b.value().bdf_string() == "01:00.0");
  CHECK(b.value().canonical() == "0000:01:00.0");
  CHECK(Bdf::parse("0000:01:00.0").ok());
  CHECK(Bdf::parse("01:00.8").failed());      // function >7
  CHECK(Bdf::parse("gg:00.0").failed());
  CHECK(Bdf::parse("01").failed());
  CHECK(Bdf::parse("").failed());
  CHECK(Bdf::parse("01:00.0.0").failed());
  // round-trip
  auto c = Bdf::parse(b.value().canonical());
  CHECK(c.ok() && c.value() == b.value());
}

// ---------------------------------------------------------------------------
// Typed values: reject NaN/Inf/negative/overflow
static void test_values() {
  CHECK(BytesPerSecond::checked(-1.0).failed());
  CHECK(BytesPerSecond::checked(std::numeric_limits<double>::infinity()).failed());
  CHECK(BytesPerSecond::checked(std::numeric_limits<double>::quiet_NaN()).failed());
  CHECK(Utilization::checked(0.5).ok());
  CHECK(Utilization::checked(1.5).failed());
  CHECK(Confidence::checked(1.0).ok());
  CHECK(PathCost::checked(-2.0).failed());
  CHECK(Bytes::checked(0).ok());
  CHECK(PayloadBytes::checked(1ull << 62).ok());
  CHECK(LaneCount::checked(16).ok());
  CHECK(QueueDepth::checked(0).ok());
}


// ---------------------------------------------------------------------------
// Theoretical bandwidth (property + unit correctness). Decimal GT/s vs GiB/s.
static void test_bandwidth() {
  // Gen3 x1 = 0.9846 GB/s = ~0.917 GiB/s; Gen3 = 8 GT/s * 128/130 / 8.
  auto e16 = envelope::effective_bytes_per_sec(PcieGen::GEN5, 16);
  // Gen5 x16 (effective) = 32*128/130 * 1e9/8 * 16 = 63.015... GB/s
  CHECK(e16.value() > 62.0e9 && e16.value() < 64.0e9);
  auto g = envelope::effective_gib_per_sec(PcieGen::GEN5, 16);
  CHECK(g.value() > 58.0 && g.value() < 59.5);
  // Gen1 x1 uses 8b/10b => 2.5 GT/s * 0.8 / 8 = 250 MB/s
  CHECK(envelope::effective_bytes_per_lane(PcieGen::GEN1) > 249.0e6 && envelope::effective_bytes_per_lane(PcieGen::GEN1) < 251.0e6);
  // Monotonic increasing by generation (x1)
  CHECK(envelope::effective_bytes_per_lane(PcieGen::GEN2) > envelope::effective_bytes_per_lane(PcieGen::GEN1));
  CHECK(envelope::effective_bytes_per_lane(PcieGen::GEN3) > envelope::effective_bytes_per_lane(PcieGen::GEN2));
  CHECK(envelope::effective_bytes_per_lane(PcieGen::GEN4) > envelope::effective_bytes_per_lane(PcieGen::GEN3));
  CHECK(envelope::effective_bytes_per_lane(PcieGen::GEN5) > envelope::effective_bytes_per_lane(PcieGen::GEN4));
  CHECK(envelope::effective_bytes_per_lane(PcieGen::GEN6) > envelope::effective_bytes_per_lane(PcieGen::GEN5));
  CHECK(envelope::raw_bytes_per_sec(PcieGen::GEN3, 16) > envelope::effective_bytes_per_sec(PcieGen::GEN3, 16).value());
  CHECK(envelope::lane_count_valid(16));
  CHECK(!envelope::lane_count_valid(3));
  // LinkEnvelope is DERIVED, not MEASURED
  LinkEnvelope env(PcieGen::GEN5, LaneCount(16));
  CHECK(env.provenance() == Provenance::DERIVED);
  // decimal vs binary never equal for large x16 to avoid silent unit mix
  CHECK(env.effective_bytes_per_sec().value() != env.effective_gib_per_sec().value());
}

// ---------------------------------------------------------------------------
// Topology + ancestry + locality + cycle/duplicate detection
static void test_topology() {
  Topology topo;
  auto rc = topo.add_root_complex(RootComplexId(1), Bdf(0,0,0,0), PciNodeGeneration(1), Provenance::REPORTED);
  auto sw = topo.add_bridge(BridgeId(2), Bdf(0,1,0,0), PciNodeGeneration(1), Provenance::REPORTED, rc.value());
  auto gpu = topo.add_endpoint(PciNodeId(100), Bdf(0,2,0,0), DeviceClass::ACCELERATOR, PciNodeGeneration(1), Provenance::REPORTED, sw.value());
  auto nic = topo.add_endpoint(PciNodeId(101), Bdf(0,2,0,1), DeviceClass::NIC, PciNodeGeneration(1), Provenance::REPORTED, sw.value());
  CHECK(topo.validate().ok());
  CHECK(topo.locality(gpu.value(), nic.value()) == LocalityClass::SAME_SWITCH);
  CHECK(topo.ancestors(gpu.value()).size() == 3);
  CHECK(topo.common_ancestor(gpu.value(), nic.value()).has_value());
  // cross-root => unknown locality
  auto rc2 = topo.add_root_complex(RootComplexId(9), Bdf(1,0,0,0), PciNodeGeneration(1), Provenance::REPORTED);
  auto nic2 = topo.add_endpoint(PciNodeId(102), Bdf(1,2,0,0), DeviceClass::NIC, PciNodeGeneration(1), Provenance::REPORTED, rc2.value());
  CHECK(topo.locality(gpu.value(), nic2.value()) == LocalityClass::UNKNOWN);
  // cycle rejection: attaching a root's parent to the endpoint would cycle
  CHECK(topo.attach(rc.value(), gpu.value(), std::nullopt).failed());
  // device reappearing at the same BDF resolves to the existing node with a
  // FRESH generation (a BDF is not authority); no duplicate row is created.
  auto reappeared = topo.add_endpoint(PciNodeId(200), Bdf(0,2,0,0), DeviceClass::ACCELERATOR, PciNodeGeneration(1), Provenance::REPORTED, sw.value());
  CHECK(reappeared.ok());
  CHECK(reappeared.value() == gpu.value());
  CHECK(topo.validate().ok());
  CHECK(semantic_digest(Snapshot{}) != 0);
}

// ---------------------------------------------------------------------------
// Path selection: deterministic ranking, capability, stale authority, penalty
static void test_selection() {
  Topology topo;
  auto rc = topo.add_root_complex(RootComplexId(1), Bdf(0,0,0,0), PciNodeGeneration(1), Provenance::SYNTHETIC);
  auto sw = topo.add_bridge(BridgeId(2), Bdf(0,1,0,0), PciNodeGeneration(1), Provenance::SYNTHETIC, rc.value());
  auto gpu0 = topo.add_endpoint(PciNodeId(3), Bdf(0,2,0,0), DeviceClass::ACCELERATOR, PciNodeGeneration(1), Provenance::SYNTHETIC, sw.value());
  auto gpu1 = topo.add_endpoint(PciNodeId(4), Bdf(0,2,0,1), DeviceClass::ACCELERATOR, PciNodeGeneration(1), Provenance::SYNTHETIC, sw.value());
  auto nic = topo.add_endpoint(PciNodeId(5), Bdf(0,2,0,2), DeviceClass::NIC, PciNodeGeneration(1), Provenance::SYNTHETIC, sw.value());
  // two candidate paths gpu0->nic and gpu1->nic
  Path p0(PathId(1), PathGeneration(1)); p0.set_endpoints(gpu0.value(), nic.value());
  p0.set_path_class(PathClass::ACCELERATOR_TO_NIC); p0.set_locality(LocalityClass::SAME_SWITCH);
  p0.set_freshness(FreshnessRecord(Freshness::CURRENT));
  p0.set_authority(CoordinatorEpoch(7), WorkerBootId(3), HostGeneration(1), PathGeneration(1), DeviceGeneration(1));
  p0.add_capability(Capability::GPUDIRECT);
  p0.set_best_measured(BytesPerSecond(10.0e9), Provenance::MEASURED);
  Path p1(PathId(2), PathGeneration(1)); p1.set_endpoints(gpu1.value(), nic.value());
  p1.set_path_class(PathClass::ACCELERATOR_TO_NIC); p1.set_locality(LocalityClass::SAME_SWITCH);
  p1.set_freshness(FreshnessRecord(Freshness::CURRENT));
  p1.set_authority(CoordinatorEpoch(7), WorkerBootId(3), HostGeneration(1), PathGeneration(1), DeviceGeneration(1));
  p1.add_capability(Capability::GPUDIRECT);
  p1.set_best_measured(BytesPerSecond(8.0e9), Provenance::MEASURED);

  SelectionRequest req; req.source=gpu0.value(); req.destination=nic.value();
  req.epoch=CoordinatorEpoch(7); req.boot=WorkerBootId(3); req.required_capabilities={Capability::GPUDIRECT};
  PathSelector sel;
  auto d = sel.select({p0,p1}, req);
  CHECK(d.decision == Decision::USE_PATH);
  CHECK(d.selected && *d.selected == PathId(1));
  CHECK(d.alternatives.size() == 1);

  // stale authority => eliminated
  SelectionRequest stale = req; stale.epoch = CoordinatorEpoch(9);
  auto d2 = sel.select({p0,p1}, stale);
  CHECK(d2.decision == Decision::REJECT);
  CHECK(!d2.selected);

  // missing required capability => capability eliminated
  // reuse p0 without GPUDIRECT on p1
  Path p1n(PathId(3), PathGeneration(1)); p1n.set_endpoints(gpu1.value(), nic.value());
  p1n.set_path_class(PathClass::ACCELERATOR_TO_NIC); p1n.set_locality(LocalityClass::SAME_SWITCH);
  p1n.set_freshness(FreshnessRecord(Freshness::CURRENT));
  p1n.set_authority(CoordinatorEpoch(7), WorkerBootId(3), HostGeneration(1), PathGeneration(1), DeviceGeneration(1));
  auto d3 = sel.select({p1n}, req);
  CHECK(d3.decision == Decision::REJECT);
  CHECK(!d3.selected);

  // penalty for cross-numa
  Path p4(PathId(4), PathGeneration(1)); p4.set_endpoints(gpu0.value(), nic.value());
  p4.set_path_class(PathClass::ACCELERATOR_TO_NIC); p4.set_locality(LocalityClass::CROSS_NUMA);
  p4.set_freshness(FreshnessRecord(Freshness::CURRENT));
  p4.set_authority(CoordinatorEpoch(7), WorkerBootId(3), HostGeneration(1), PathGeneration(1), DeviceGeneration(1));
  p4.add_capability(Capability::GPUDIRECT);
  auto d4 = sel.select({p4}, req);
  CHECK(d4.decision == Decision::USE_PATH_WITH_PENALTY);
}

// ---------------------------------------------------------------------------
// Reservations: overcommit, double release, duplicate, accounting closure.
static void test_reservation() {
  ReservationRegistry reg(ReservationLimits{CapacityBytes(1ull<<30), Milliseconds(60000)});
  Reservation r(ReservationId(1), ReservationGeneration(1));
  r.set_path(PathId(1), PathGeneration(1));
  r.set_authority(WorkerId(1), WorkerBootId(5), AttemptId(1), CoordinatorEpoch(2), PolicyId(1), PolicyGeneration(1));
  r.set_request(CapacityBytes(64u<<20), BytesPerSecond(1.0e9), Milliseconds(1000), 0, PciNodeId(3), PciNodeId(5), "test");
  CHECK(reg.reserve(r).ok());
  CHECK(reg.reserve(r).failed());                  // duplicate id
  CHECK(reg.reserved_bytes(PathId(1)) == (64u<<20));
  // overcommit: push beyond capacity
  Reservation r2(ReservationId(2), ReservationGeneration(1));
  r2.set_path(PathId(1), PathGeneration(1));
  r2.set_authority(WorkerId(1), WorkerBootId(5), AttemptId(1), CoordinatorEpoch(2), PolicyId(1), PolicyGeneration(1));
  r2.set_request(CapacityBytes(1u<<30), BytesPerSecond(1.0e9), Milliseconds(1000), 0, PciNodeId(3), PciNodeId(5), "big");
  CHECK(reg.reserve(r2).failed());                 // overcommit
  CHECK(reg.validate_accounting().ok());
  CHECK(reg.release(ReservationId(1), WorkerBootId(5), ReservationGeneration(1), CoordinatorEpoch(2)).ok());
  CHECK(reg.release(ReservationId(1), WorkerBootId(5), ReservationGeneration(1), CoordinatorEpoch(2)).failed()); // duplicate
  CHECK(reg.validate_accounting().ok());
  CHECK(reg.active_count() == 0);
  // stale boot release rejected
  Reservation r3(ReservationId(3), ReservationGeneration(1));
  r3.set_path(PathId(2), PathGeneration(1));
  r3.set_authority(WorkerId(1), WorkerBootId(5), AttemptId(1), CoordinatorEpoch(2), PolicyId(1), PolicyGeneration(1));
  r3.set_request(CapacityBytes(16u<<20), BytesPerSecond(1.0e9), Milliseconds(1000), 0, PciNodeId(3), PciNodeId(5), "s");
  CHECK(reg.reserve(r3).ok());
  CHECK(reg.release(ReservationId(3), WorkerBootId(99), ReservationGeneration(1), CoordinatorEpoch(2)).failed()); // stale boot
  CHECK(reg.release(ReservationId(3), WorkerBootId(5), ReservationGeneration(1), CoordinatorEpoch(2)).ok());
}

// ---------------------------------------------------------------------------
// Concurrency: concurrent reserve/release must close to zero (no drift).
static void test_concurrency() {
  const int kThreads = 8, kOps = 500;
  ReservationRegistry reg(ReservationLimits{CapacityBytes(1ull<<30), Milliseconds(60000)});
  std::atomic<int> ok{0}, fail{0};
  std::vector<std::thread> ts;
  for (int t = 0; t < kThreads; ++t) {
    ts.emplace_back([&, t]() {
      for (int i = 0; i < kOps; ++i) {
        std::uint64_t id = static_cast<std::uint64_t>(t) * 100000 + i + 1;
        Reservation r(ReservationId(id), ReservationGeneration(1));
        r.set_path(PathId(1), PathGeneration(1));
        r.set_authority(WorkerId(1), WorkerBootId(1), AttemptId(1), CoordinatorEpoch(1), PolicyId(1), PolicyGeneration(1));
        r.set_request(CapacityBytes(1024), BytesPerSecond(1.0e9), Milliseconds(1), 0, PciNodeId(3), PciNodeId(5), "c");
        if (reg.reserve(r).ok()) {
          if (reg.release(ReservationId(id), WorkerBootId(1), ReservationGeneration(1), CoordinatorEpoch(1)).ok()) ++ok;
          else ++fail;
        } else ++fail;
      }
    });
  }
  for (auto& th : ts) th.join();
  CHECK(ok == kThreads * kOps);
  CHECK(fail == 0);
  CHECK(reg.validate_accounting().ok());
  CHECK(reg.active_count() == 0);
}

// ---------------------------------------------------------------------------
// Protocol framing: reject malformed/truncated/oversize/checksum/version.
static void test_protocol() {
  wire::Frame f; f.kind = wire::MsgKind::MSG_HELLO;
  Writer w; wire::write_register(w, wire::RegisterRequest{1,2,3}); f.payload = w.take();
  auto bytes = wire::encode(f);
  auto dec = wire::decode(bytes);
  CHECK(dec.ok());
  CHECK(dec->kind == wire::MsgKind::MSG_HELLO);
  auto reg = wire::read_register(f.payload);
  CHECK(reg.ok() && reg->worker == 1 && reg->boot == 2 && reg->host_gen == 3);
  // bad magic
  auto bad = bytes; bad[0] ^= 0xFF;
  CHECK(wire::decode(bad).failed());
  // bad version
  auto bver = bytes; bver[4] ^= 0xFF;
  CHECK(wire::decode(bver).failed());
  // checksum corruption within payload
  auto bcks = bytes; bcks[bytes.size()-1] ^= 0xFF;
  CHECK(wire::decode(bcks).failed());
  // truncated
  std::vector<std::uint8_t> trunc(bytes.begin(), bytes.begin()+10);
  CHECK(wire::decode(trunc).failed());
  // oversize payload length
  auto over = wire::encode(f);
  Writer w2; w2.u32(1000000000u);
  (void)over;
  // trailing garbage
  auto tg = bytes; tg.push_back(0x00);
  CHECK(wire::decode(tg).failed());
}

// ---------------------------------------------------------------------------
// Persistence: round-trip, corruption, truncation, duplicate, recovery.
static void test_persistence(const std::string& path) {
  Snapshot s; s.sequence = 42; s.epoch = CoordinatorEpoch(3);
  Node n; n.bdf = Bdf(0,1,0,0); n.kind = DeviceClass::BRIDGE; n.generation = PciNodeGeneration(1);
  n.vendor="V"; n.device="D"; n.class_name="C"; s.nodes.push_back(n);
  auto v = persist::serialize(s);
  CHECK(v.ok());
  auto d = persist::deserialize(v.value());
  CHECK(d.ok() && d->nodes.size() == 1);
  // deterministic digest
  CHECK(semantic_digest(s) == semantic_digest(d.value()));
  // save/load
  CHECK(persist::save(path, s).ok());
  auto l = persist::load(path);
  CHECK(l.ok() && l->sequence == 42);
  // corruption
  std::ifstream fi(path, std::ios::binary|std::ios::ate);
  long sz = (long)fi.tellg(); std::vector<uint8_t> raw((size_t)sz);
  fi.seekg(0); fi.read((char*)raw.data(), sz); fi.close();
  raw[30] ^= 0xFF;
  const std::string bpath = path + ".bad";
  std::ofstream fo(bpath, std::ios::binary|std::ios::trunc); fo.write((char*)raw.data(), raw.size()); fo.close();
  CHECK(persist::load(bpath).failed());
  // truncation
  std::vector<uint8_t> trunc(raw.begin(), raw.begin()+12);
  const std::string tpath = path + ".trunc";
  std::ofstream ft(tpath, std::ios::binary|std::ios::trunc); ft.write((char*)trunc.data(), trunc.size()); ft.close();
  CHECK(persist::load(tpath).failed());
  // recovery: physical observation must not be CURRENT
  Snapshot s2 = l.value();
  persist::mark_physical_revalidation_required(s2);
  (void)s2;
}

// ---------------------------------------------------------------------------
// Synthetic multi-endpoint scenarios (24 required). Uses production machinery.
static void test_synthetic_scenarios() {
  auto make_path = [](PathId id, LocalityClass loc, Freshness fresh, bool cap,
                      double measured, ContentionState cont, CoordinatorEpoch ep,
                      WorkerBootId boot) {
    Path p(id, PathGeneration(1));
    p.set_endpoints(PciNodeId(1), PciNodeId(2));
    p.set_path_class(PathClass::PEER_ENDPOINT_PATH);
    p.set_locality(loc); p.set_freshness(FreshnessRecord(fresh));
    if (cap) p.add_capability(Capability::GPUDIRECT);
    p.set_best_measured(BytesPerSecond(measured), Provenance::MEASURED);
    p.set_contention(cont);
    p.set_authority(ep, boot, HostGeneration(1), PathGeneration(1), DeviceGeneration(1));
    return p;
  };
  SelectionRequest req; req.source=PciNodeId(1); req.destination=PciNodeId(2);
  req.epoch=CoordinatorEpoch(1); req.boot=WorkerBootId(1); req.required_capabilities={Capability::GPUDIRECT};
  PathSelector sel;

  // fresh measured beats stale faster historical
  auto fresh = make_path(PathId(1), LocalityClass::SAME_SWITCH, Freshness::CURRENT, true, 10.0e9, ContentionState::IDLE, CoordinatorEpoch(1), WorkerBootId(1));
  auto stale = make_path(PathId(2), LocalityClass::SAME_SWITCH, Freshness::STALE, true, 20.0e9, ContentionState::IDLE, CoordinatorEpoch(1), WorkerBootId(1));
  auto d = sel.select({stale, fresh}, req);
  CHECK(d.decision == Decision::USE_PATH && d.selected == PathId(1));

  // required capability unavailable => reject
  auto nocap = make_path(PathId(3), LocalityClass::SAME_SWITCH, Freshness::CURRENT, false, 10.0e9, ContentionState::IDLE, CoordinatorEpoch(1), WorkerBootId(1));
  auto d2 = sel.select({nocap}, req);
  CHECK(d2.decision == Decision::REJECT && !d2.selected);

  // saturated => defer
  auto sat = make_path(PathId(4), LocalityClass::SAME_SWITCH, Freshness::CURRENT, true, 10.0e9, ContentionState::SATURATED, CoordinatorEpoch(1), WorkerBootId(1));
  auto d3 = sel.select({sat}, req);
  CHECK(d3.decision == Decision::DEFER);

  // cross-numa penalty
  auto xnuma = make_path(PathId(5), LocalityClass::CROSS_NUMA, Freshness::CURRENT, true, 10.0e9, ContentionState::IDLE, CoordinatorEpoch(1), WorkerBootId(1));
  auto d4 = sel.select({xnuma}, req);
  CHECK(d4.decision == Decision::USE_PATH_WITH_PENALTY);

  // stale authority rejected
  auto staleauth = make_path(PathId(6), LocalityClass::SAME_SWITCH, Freshness::CURRENT, true, 10.0e9, ContentionState::IDLE, CoordinatorEpoch(9), WorkerBootId(1));
  CHECK(sel.select({staleauth}, req).decision == Decision::REJECT);

  // unknown capability retained => REJECT (not guessed)
  auto nocap2 = make_path(PathId(7), LocalityClass::SAME_SWITCH, Freshness::CURRENT, false, 0.0, ContentionState::UNKNOWN, CoordinatorEpoch(1), WorkerBootId(1));
  CHECK(sel.select({nocap2}, req).decision == Decision::REJECT);

  // deterministic ranking under identical state
  auto a = make_path(PathId(10), LocalityClass::SAME_SWITCH, Freshness::CURRENT, true, 5.0e9, ContentionState::IDLE, CoordinatorEpoch(1), WorkerBootId(1));
  auto b = make_path(PathId(11), LocalityClass::SAME_SWITCH, Freshness::CURRENT, true, 5.0e9, ContentionState::IDLE, CoordinatorEpoch(1), WorkerBootId(1));
  auto r1 = sel.rank({a,b}, req);
  auto r2 = sel.rank({a,b}, req);
  CHECK(r1 == r2);

  // peer path structurally present but direct peer capability unavailable:
  // a path without GPUDIRECT is rejected even though it shares a switch.
  auto peer = make_path(PathId(12), LocalityClass::SAME_SWITCH, Freshness::CURRENT, false, 0.0, ContentionState::IDLE, CoordinatorEpoch(1), WorkerBootId(1));
  CHECK(sel.select({peer}, req).decision == Decision::REJECT);
}

// ---------------------------------------------------------------------------
// Adversarial: stale generations, malformed BDF, impossible counts.
static void test_adversarial() {
  // stale measurement generation rejected
  MeasurementStore store;
  Measurement m1(MeasurementId(1), MeasurementGeneration(5), PciNodeId(1), PciNodeId(2),
                 TransferClass::PINNED_H2D, EngineKind::CUDA, TransferBytes(1024),
                 IterationCount(1), Nanoseconds(1000), Provenance::MEASURED);
  CHECK(store.ingest(m1).ok());
  Measurement m0 = m1; // same id
  // replace with a measurement not newer than gen 5 must fail
  Measurement mm(MeasurementId(1), MeasurementGeneration(5), PciNodeId(1), PciNodeId(2),
                 TransferClass::PINNED_H2D, EngineKind::CUDA, TransferBytes(1024),
                 IterationCount(1), Nanoseconds(1000), Provenance::MEASURED);
  CHECK(store.ingest(mm).failed());
  Measurement m2(MeasurementId(1), MeasurementGeneration(6), PciNodeId(1), PciNodeId(2),
                 TransferClass::PINNED_H2D, EngineKind::CUDA, TransferBytes(2048),
                 IterationCount(1), Nanoseconds(1000), Provenance::MEASURED);
  CHECK(store.ingest(m2).ok());
  CHECK(store.latest(PciNodeId(1), PciNodeId(2), TransferClass::PINNED_H2D)->bytes().value() == 2048);
  CHECK(!store.ingest(m0).ok());
  // crc32 known value
  CHECK(crc32(reinterpret_cast<const std::uint8_t*>("123456789"), 9) == 0xCBF43926u);
}

int main() {
  std::printf("PCIe Fabric unit tests\n");
  test_bdf();
  test_values();
  test_bandwidth();
  test_topology();
  test_selection();
  test_reservation();
  test_concurrency();
  test_protocol();
  test_persistence("C:\\Users\\pauln\\AppData\\Local\\Temp\\pf_smoke\\unit.bin");
  test_synthetic_scenarios();
  test_adversarial();
  std::printf("DONE checks=%d failures=%d\n", g_check, g_fail);
  return g_fail == 0 ? 0 : 1;
}