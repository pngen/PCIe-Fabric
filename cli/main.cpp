// SPDX-License-Identifier: Apache-2.0
// pf — PCIe Fabric CLI. A thin front end over the library; never duplicates
// core logic.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "pciefabric/bandwidth.hpp"
#include "pciefabric/bdf.hpp"
#include "pciefabric/coordinator.hpp"
#include "pciefabric/cuda_engine.hpp"
#include "pciefabric/path.hpp"
#include "pciefabric/result.hpp"
#include "pciefabric/runtime.hpp"
#include "pciefabric/selection.hpp"
#include "pciefabric/values.hpp"
#include "pciefabric/windows_enum.hpp"
#include "pciefabric/worker.hpp"

using namespace pci;

static int cmd_validate_bdf(int argc, char** argv) {
  if (argc < 3) { printf("usage: pf validate-bdf <bdf>\n"); return 1; }
  auto b = Bdf::parse(argv[2]);
  if (!b) { printf("invalid BDF: %s\n", b.message().c_str()); return 1; }
  printf("%s\n", b.value().canonical().c_str());
  return 0;
}

static int cmd_bandwidth(int argc, char** argv) {
  if (argc < 4) { printf("usage: pf bandwidth <gen> <lanes>\n"); return 1; }
  auto lanes = static_cast<std::uint16_t>(std::atoi(argv[3]));
  std::string g = argv[2];
  auto pick = [&]() -> PcieGen {
    if (g == "gen1") return PcieGen::GEN1; if (g == "gen2") return PcieGen::GEN2;
    if (g == "gen3") return PcieGen::GEN3; if (g == "gen4") return PcieGen::GEN4;
    if (g == "gen5") return PcieGen::GEN5; if (g == "gen6") return PcieGen::GEN6;
    return PcieGen::UNKNOWN;
  };
  PcieGen gen = pick();
  if (gen == PcieGen::UNKNOWN) { printf("unknown generation\n"); return 1; }
  LinkEnvelope env(gen, LaneCount(lanes));
  printf("%s x%u (DERIVED): raw=%.1f GB/s effective=%.1f GB/s = %.3f GiB/s\n",
         to_string(gen).c_str(), lanes, env.raw_bytes_per_sec().value()/1.0e9,
         env.effective_bytes_per_sec().value()/1.0e9, env.effective_gib_per_sec().value());
  return 0;
}

static int cmd_discover(int argc, char** argv) {
  (void)argc; (void)argv;
  auto devs = enumerate_pci_devices();
  if (!devs) { printf("discovery failed: %s\n", devs.message().c_str()); return 1; }
  printf("%zu PCI devices:\n", devs.value().size());
  for (const auto& d : devs.value()) {
    printf("  %-14s kind=%-12s vendor=%s dev=%s bridge=%d parent=%s\n",
           d.bdf.canonical().c_str(), to_string(d.kind).c_str(), d.vendor_id.c_str(),
           d.device_id.c_str(), (int)d.is_bridge, d.parent_bdf ? d.parent_bdf->canonical().c_str() : "-");
  }
  return 0;
}

static int cmd_cuda_proof(int argc, char** argv) {
  std::uint64_t bytes = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : (256ull << 20);
  std::uint64_t iters = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 8;
#ifdef PCIEFABRIC_HAVE_CUDA
  auto d = cudap::detect_device();
  if (!d) { printf("no CUDA device: %s\n", d.message().c_str()); return 1; }
  printf("CUDA device: %s  bdf=%-14s cc=%d.%d\n", d.value().name.c_str(),
         d.value().bdf.canonical().c_str(), d.value().cc_major, d.value().cc_minor);
  auto p = cudap::run_proof(bytes, iters);
  if (!p) { printf("proof failed: %s\n", p.message().c_str()); return 1; }
  auto& r = p.value();
  printf("pageable H2D %.2f GiB/s  pageable D2H %.2f GiB/s\n",
         r.pageable_h2d.bytes_per_sec / (1024.0*1024.0*1024.0), r.pageable_d2h.bytes_per_sec / (1024.0*1024.0*1024.0));
  printf("pinned   H2D %.2f GiB/s  pinned   D2H %.2f GiB/s\n",
         r.pinned_h2d.bytes_per_sec / (1024.0*1024.0*1024.0), r.pinned_d2h.bytes_per_sec / (1024.0*1024.0*1024.0));
  printf("kernel_verified=%d mem_clean=%d cuda_clean=%d pci_correlated=%d\n",
         (int)r.kernel_verified, (int)r.mem_clean, (int)r.cuda_clean, (int)r.pci_correlated);
  return (r.ok && r.kernel_verified) ? 0 : 2;
#else
  (void)bytes; (void)iters;
  printf("CUDA engine not built (PCIeFabric built without CUDA).\n");
  return 1;
#endif
}

static int cmd_topology(int argc, char** argv) {
  (void)argc; (void)argv;
  Runtime rt;
  rt.set_authority(CoordinatorEpoch(1), WorkerBootId(1), HostGeneration(1), 1);
  auto rc = rt.add_endpoint(PciNodeId(1), Bdf(0,0,0,0), DeviceClass::ROOT_COMPLEX, PciNodeGeneration(1), Provenance::SYNTHETIC, PciNodeId(0));
  auto sw = rt.add_endpoint(PciNodeId(2), Bdf(0,1,0,0), DeviceClass::BRIDGE, PciNodeGeneration(1), Provenance::SYNTHETIC, rc.value());
  auto g0 = rt.add_endpoint(PciNodeId(3), Bdf(0,2,0,0), DeviceClass::ACCELERATOR, PciNodeGeneration(1), Provenance::SYNTHETIC, sw.value());
  auto g1 = rt.add_endpoint(PciNodeId(4), Bdf(0,2,0,1), DeviceClass::ACCELERATOR, PciNodeGeneration(1), Provenance::SYNTHETIC, sw.value());
  auto nic = rt.add_endpoint(PciNodeId(5), Bdf(0,2,0,2), DeviceClass::NIC, PciNodeGeneration(1), Provenance::SYNTHETIC, sw.value());
  rt.build_paths();
  printf("synthetic topology: endpoints=%zu paths=%zu\n", rt.endpoint_count(), rt.path_count());
  SelectionRequest req; req.source = g0.value(); req.destination = nic.value();
  req.epoch = CoordinatorEpoch(1); req.boot = WorkerBootId(1);
  auto d = rt.select(req);
  printf("preferred path for gpu->nic: decision=%s selected=%s\n", to_string(d.decision).c_str(),
         d.selected ? std::to_string(d.selected->value()).c_str() : "-");
  return 0;
}

static int cmd_save_load(int argc, char** argv) {
  if (argc < 4) { printf("usage: pf save|load <path>\n"); return 1; }
  std::string path = argv[3];
  Runtime rt;
  rt.set_authority(CoordinatorEpoch(1), WorkerBootId(1), HostGeneration(1), 1);
  auto rc = rt.add_endpoint(PciNodeId(1), Bdf(0,0,0,0), DeviceClass::ROOT_COMPLEX, PciNodeGeneration(1), Provenance::SYNTHETIC, PciNodeId(0));
  auto sw = rt.add_endpoint(PciNodeId(2), Bdf(0,1,0,0), DeviceClass::BRIDGE, PciNodeGeneration(1), Provenance::SYNTHETIC, rc.value());
  rt.add_endpoint(PciNodeId(3), Bdf(0,2,0,0), DeviceClass::ACCELERATOR, PciNodeGeneration(1), Provenance::SYNTHETIC, sw.value());
  rt.build_paths();
  if (std::string(argv[2]) == "save") {
    auto r = rt.save(path);
    if (!r) { printf("save failed: %s\n", r.message().c_str()); return 1; }
    printf("saved snapshot to %s (paths=%zu)\n", path.c_str(), rt.path_count());
  } else {
    auto s = rt.load(path);
    if (!s) { printf("load failed: %s code=%d\n", s.message().c_str(), (int)s.code()); return 1; }
    printf("loaded snapshot sequence=%llu digest=%llu; recovered physical observations -> REVALIDATION_REQUIRED\n",
           (unsigned long long)s->sequence, (unsigned long long)s->semantic_digest);
  }
  return 0;
}

static int cmd_coordinator(int argc, char** argv) {
  std::uint16_t port = argc >= 3 ? static_cast<std::uint16_t>(std::atoi(argv[2])) : 0;
  Coordinator c;
  auto r = c.start(port);
  if (!r) { printf("coordinator start failed: %s\n", r.message().c_str()); return 1; }
  printf("coordinator started on port %u\n", c.port());
  c.run();
  return 0;
}

static int cmd_worker(int argc, char** argv) {
  // pf worker --id N --boot B --port P
  std::uint64_t id = 1, boot = 1, pport = 0;
  for (int i = 2; i + 1 < argc; ++i) {
    std::string k = argv[i];
    if (k == "--id") id = std::strtoull(argv[i+1], nullptr, 10);
    else if (k == "--boot") boot = std::strtoull(argv[i+1], nullptr, 10);
    else if (k == "--port") pport = std::strtoull(argv[i+1], nullptr, 10);
    else if (k == "--save") boot = std::strtoull(argv[i+1], nullptr, 10);
  }
  Worker w(WorkerId(id), WorkerBootId(boot), "127.0.0.1", static_cast<std::uint16_t>(pport), 1);
  auto c = w.connect(); if (!c) { printf("worker connect failed: %s\n", c.message().c_str()); return 1; }
  auto reg = w.register_self(); if (!reg) { printf("worker register failed: %s\n", reg.message().c_str()); return 1; }
  Snapshot s; s.epoch = w.epoch();
  w.send_inventory(s);
  printf("worker id=%llu boot=%llu registered epoch=%llu\n", (unsigned long long)id, (unsigned long long)boot, (unsigned long long)w.epoch().value());
  // heartbeat loop
  for (int i = 0; i < 120; ++i) { w.heartbeat(); } // ~bounded
  w.close();
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) { printf("PCIe Fabric 1.0.0\nusage: pf <command> [args]\n"); return 0; }
  std::string cmd = argv[1];
  if (cmd == "validate-bdf") return cmd_validate_bdf(argc, argv);
  if (cmd == "bandwidth") return cmd_bandwidth(argc, argv);
  if (cmd == "discover") return cmd_discover(argc, argv);
  if (cmd == "cuda-proof") return cmd_cuda_proof(argc, argv);
  if (cmd == "topology" || cmd == "paths") return cmd_topology(argc, argv);
  if (cmd == "save" || cmd == "load") return cmd_save_load(argc, argv);
  if (cmd == "coordinator") return cmd_coordinator(argc, argv);
  if (cmd == "worker") return cmd_worker(argc, argv);
  printf("unknown command: %s\n", cmd.c_str());
  return 1;
}