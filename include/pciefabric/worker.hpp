#pragma once
// PCIe Fabric worker: a per-host observation agent. A fresh WorkerBootId is
// generated on every process start so authority cannot carry across restarts.
#include <cstdint>
#include <string>
#include <vector>

#include "pciefabric/ids.hpp"
#include "pciefabric/path.hpp"
#include "pciefabric/protocol.hpp"
#include "pciefabric/result.hpp"
#include "pciefabric/snapshot.hpp"
#include "pciefabric/tcp.hpp"

namespace pci {

class Worker {
public:
  Worker(WorkerId id, WorkerBootId boot, std::string host, std::uint16_t port,
         std::uint64_t host_gen);
  ~Worker();

  Result<void> connect();
  Result<void> register_self();
  Result<void> send_inventory(const Snapshot& snap);
  Result<void> send_path_snapshot(const std::vector<Path>& paths, CoordinatorEpoch epoch,
                                  WorkerBootId boot);
  Result<void> send_measurement(const Measurement& m);
  Result<Reservation> reserve(const Reservation& proposed);
  Result<void> release(ReservationId id, ReservationGeneration gen);
  Result<void> heartbeat();
  Result<void> revalidate();
  Result<void> close();

  WorkerId id() const noexcept { return id_; }
  WorkerBootId boot() const noexcept { return boot_; }
  std::uint16_t port() const noexcept { return port_; }
  CoordinatorEpoch epoch() const noexcept { return epoch_; }

private:
  Result<wire::Ack> send_and_await_ack(const wire::Frame& f);

  WorkerId id_{};
  WorkerBootId boot_{};
  std::string host_;
  std::uint16_t port_{};
  HostGeneration host_gen_{};
  CoordinatorEpoch epoch_{};
  TcpSocket sock_;
  bool connected_{false};
};

}  // namespace pci
