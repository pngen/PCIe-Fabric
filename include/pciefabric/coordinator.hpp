#pragma once
// PCIe Fabric coordinator: owns canonical path/inventory authority, fences all
// mutable distributed operations, and serves the framed-TCP protocol.
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "pciefabric/ids.hpp"
#include "pciefabric/measurement.hpp"
#include "pciefabric/path.hpp"
#include "pciefabric/protocol.hpp"
#include "pciefabric/reservation.hpp"
#include "pciefabric/result.hpp"
#include "pciefabric/snapshot.hpp"
#include "pciefabric/tcp.hpp"
#include "pciefabric/topology.hpp"

namespace pci {

struct WorkerInfo {
  WorkerId id{};
  WorkerBootId boot{};
  HostGeneration host_gen{};
  bool connected{false};
  bool revalidation_required{false};
  EpochMs last_seen{};
};

class Coordinator {
public:
  Coordinator() = default;
  ~Coordinator();

  Result<void> start(std::uint16_t port);
  // Run the accept loop until request_stop(). Returns when stopped.
  Result<void> run();
  void request_stop() { stop_.store(true); }

  // --- authority ---
  CoordinatorEpoch epoch() const;
  void advance_epoch();

  // --- state (thread-safe snapshots for tests/proofs) ---
  std::size_t worker_count() const;
  std::size_t path_count() const;
  std::size_t reservation_active_count() const;
  std::size_t measurement_count() const;
  Result<void> validate_accounting() const;

  // Public driver for the multiprocess proof / CLI.
  Result<void> revalidate_all();
  std::uint16_t port() const noexcept { return port_; }

  // Snapshot of current coordinator state (for persistence / recovery).
  Snapshot snapshot() const;
  Result<void> set_persist_path(const std::string& p) { persist_path_ = p; return Result<void>::success(); }
  Result<void> persist_now() const;

private:
  struct ConnContext {
    TcpSocket socket;
    std::thread thread;
  };
  void handle_connection(TcpSocket sock);
  Result<void> handle_frame(TcpSocket& sock, const wire::Frame& f, std::optional<WorkerBootId>& worker_boot, std::optional<WorkerId>& worker_id);
  Result<void> dispatch_register(TcpSocket&, const wire::Frame&);
  Result<void> dispatch_inventory(TcpSocket&, const wire::Frame&);
  Result<void> dispatch_reserve(TcpSocket&, const wire::Frame&, const WorkerBootId&);
  Result<void> dispatch_release(TcpSocket&, const wire::Frame&, const WorkerBootId&);
  Result<void> reply_ack(TcpSocket&, std::uint64_t seq, bool ok, const std::string& msg);

  std::uint16_t port_{0};
  std::atomic<bool> stop_{false};
  mutable std::mutex mu_;
  TcpListener listener_{0};
  CoordinatorEpoch epoch_{};
  std::uint64_t seq_{};
  Topology topology_;
  std::map<PathId, Path> paths_;
  MeasurementStore measurements_;
  ReservationRegistry reservations_{ReservationLimits{CapacityBytes(1ull << 40), Milliseconds(3600000)}};
  std::map<WorkerId, WorkerInfo> workers_;
  std::map<WorkerId, std::weak_ptr<ConnContext>> live_conns_;
  std::vector<std::shared_ptr<ConnContext>> conns_;
  std::string persist_path_;
  std::vector<std::thread> threads_;
};
}  // namespace pci
