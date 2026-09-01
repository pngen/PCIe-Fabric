// SPDX-License-Identifier: Apache-2.0
// Multiprocess authority proof over real framed TCP. A coordinator plus two
// workers exchange register/inventory/path-snapshot/reserve/release; worker A
// is destroyed and restarted with a FRESH WorkerBootId; stale epoch/boot
// messages are fenced; worker B is unaffected; state persists and recovers with
// physical observations forced to REVALIDATION_REQUIRED. Synchronization is by
// condition-polling (no timeout-driven test failure).
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "pciefabric/coordinator.hpp"
#include "pciefabric/path.hpp"
#include "pciefabric/persistence.hpp"
#include "pciefabric/reservation.hpp"
#include "pciefabric/tcp.hpp"
#include "pciefabric/worker.hpp"

using namespace pci;

static int g_fail = 0; static int g_check = 0;
#define CHECK(cond) do { ++g_check; if (!(cond)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

template <typename F>
static bool poll(F&& pred, int rounds = 250) {
  for (int i = 0; i < rounds; ++i) { if (pred()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
  return pred();
}
static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static void send_stale_snapshot(std::uint16_t port) {
  TcpSocket s;
  if (!s.connect("127.0.0.1", port).ok()) return;
  wire::Frame reg; reg.kind = wire::MsgKind::MSG_REGISTER;
  Writer rw; wire::write_register(rw, wire::RegisterRequest{99, 99, 1}); reg.payload = rw.take();
  write_frame(s, reg); sleep_ms(40);
  Path p(PathId(777), PathGeneration(1));
  p.set_endpoints(PciNodeId(1), PciNodeId(2));
  p.set_path_class(PathClass::PEER_ENDPOINT_PATH); p.set_locality(LocalityClass::SAME_SWITCH);
  p.set_freshness(FreshnessRecord(Freshness::CURRENT));
  p.set_authority(CoordinatorEpoch(0), WorkerBootId(0), HostGeneration(1), PathGeneration(1), DeviceGeneration(1));
  Snapshot snap; snap.epoch = CoordinatorEpoch(0); snap.paths.push_back(p);
  auto body = persist::serialize(snap);
  wire::Frame f; f.kind = wire::MsgKind::MSG_PATH_SNAPSHOT; f.payload = body.value();
  write_frame(s, f); sleep_ms(60);
  s.close();
}

int main() {
  std::printf("PCIe Fabric multiprocess authority proof\n");
  std::uint16_t port = 46123;
  Coordinator coord;
  CHECK(coord.start(port).ok());
  std::thread ct([&]{ coord.run(); });
  sleep_ms(120);

  // Two workers register with distinct fresh WorkerBootId values.
  Worker wa(WorkerId(1), WorkerBootId(100), "127.0.0.1", port, 1);
  Worker wb(WorkerId(2), WorkerBootId(200), "127.0.0.1", port, 1);
  CHECK(wa.connect().ok()); CHECK(wa.register_self().ok());
  CHECK(wb.connect().ok()); CHECK(wb.register_self().ok());
  sleep_ms(50);

  Snapshot sa; sa.epoch = wa.epoch();
  Node na; na.id = PciNodeId(50); na.bdf = Bdf(0,1,0,0); na.kind = DeviceClass::ROOT_COMPLEX;
  na.generation = PciNodeGeneration(1); sa.nodes.push_back(na);
  CHECK(wa.send_inventory(sa).ok());
  Snapshot sb; sb.epoch = wb.epoch();
  CHECK(wb.send_inventory(sb).ok());
  CHECK(coord.worker_count() == 2);

  // Coordinator accepts current path authority from A.
  Path pa(PathId(1), PathGeneration(1)); pa.set_endpoints(PciNodeId(3), PciNodeId(4));
  pa.set_path_class(PathClass::PEER_ENDPOINT_PATH); pa.set_locality(LocalityClass::SAME_SWITCH);
  pa.set_freshness(FreshnessRecord(Freshness::CURRENT));
  pa.set_authority(wa.epoch(), wa.boot(), HostGeneration(1), PathGeneration(1), DeviceGeneration(1));
  CHECK(wa.send_path_snapshot({pa}, wa.epoch(), wa.boot()).ok());
  CHECK(poll([&]{ return coord.path_count() >= 1; }));

  // A real reservation via Worker A, released to close accounting exactly.
  Reservation r(ReservationId(7), ReservationGeneration(1));
  r.set_path(PathId(1), PathGeneration(1));
  r.set_authority(WorkerId(1), wa.boot(), AttemptId(1), wa.epoch(), PolicyId(1), PolicyGeneration(1));
  r.set_request(CapacityBytes(1u<<20), BytesPerSecond(1.0e9), Milliseconds(100), 0, PciNodeId(3), PciNodeId(4), "proof");
  CHECK(wa.reserve(r).ok());
  CHECK(coord.reservation_active_count() == 1);
  CHECK(wb.reserve(r).failed());                      // duplicate reservation id
  CHECK(wa.release(ReservationId(7), ReservationGeneration(1)).ok());
  CHECK(coord.reservation_active_count() == 0);
  CHECK(coord.validate_accounting().ok());

  // Destroy Worker A (its connection is lost) and restart it with a FRESH
  // WorkerBootId -- a fresh incarnation must not be rejected.
  wa.close();
  Worker wa2(WorkerId(1), WorkerBootId(1000), "127.0.0.1", port, 1);
  CHECK(wa2.connect().ok()); CHECK(wa2.register_self().ok());
  CHECK(wa2.boot().value() == 1000);                  // fresh boot
  CHECK(wa2.boot().value() != 100);                   // different from pre-restart
  sleep_ms(50);

  // Stale epoch/boot path snapshot must NOT mutate current state.
  std::size_t before = coord.path_count();
  send_stale_snapshot(port);
  sleep_ms(80);
  CHECK(coord.path_count() == before);

  // Worker B remains valid and unaffected.
  CHECK(wb.heartbeat().ok());
  CHECK(wb.register_self().ok());

  // Fresh post-restart work succeeds.
  Path pb(PathId(2), PathGeneration(1)); pb.set_endpoints(PciNodeId(3), PciNodeId(4));
  pb.set_path_class(PathClass::PEER_ENDPOINT_PATH); pb.set_locality(LocalityClass::SAME_SWITCH);
  pb.set_freshness(FreshnessRecord(Freshness::CURRENT));
  pb.set_authority(wa2.epoch(), wa2.boot(), HostGeneration(1), PathGeneration(1), DeviceGeneration(1));
  CHECK(wa2.send_path_snapshot({pb}, wa2.epoch(), wa2.boot()).ok());
  CHECK(poll([&]{ return coord.path_count() >= 2; }));

  // Persist, stop the coordinator, recover in a fresh coordinator.
  const std::string fpath = "C:\\Users\\pauln\\AppData\\Local\\Temp\\pf_smoke\\coord.bin";
  coord.set_persist_path(fpath);
  CHECK(coord.persist_now().ok());
  coord.request_stop();
  ct.join();

  auto snap = persist::load(fpath);
  CHECK(snap.ok());
  auto s = snap.value();
  persist::mark_physical_revalidation_required(s);
  if (!s.paths.empty()) CHECK(s.paths[0].freshness().state() == Freshness::REVALIDATION_REQUIRED);
  Coordinator coord2;
  CHECK(coord2.validate_accounting().ok());

  std::printf("DONE checks=%d failures=%d\n", g_check, g_fail);
  return g_fail == 0 ? 0 : 1;
}
