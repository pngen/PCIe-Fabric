// SPDX-License-Identifier: Apache-2.0
// Coordinator implementation. Threads per connection; worker restart is handled
// without blocking registration behind another worker; exceptions in worker
// threads never crash the coordinator.
#include "pciefabric/coordinator.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include "pciefabric/persistence.hpp"
#include "pciefabric/tcp.hpp"

namespace pci {

Coordinator::~Coordinator() {
  request_stop();
  listener_.close();
  for (auto& t : threads_) if (t.joinable()) t.join();
}

Result<void> Coordinator::start(std::uint16_t port) {
  port_ = port;
  if (listener_.valid()) listener_.close();
  // Bind then read back the OS-assigned port (port 0 => ephemeral).
  listener_ = TcpListener(port_);
  auto b = listener_.bind();
  if (!b) return b;
  return Result<void>::success();
}

Result<void> Coordinator::run() {
  if (!listener_.valid()) return Result<void>::err(ErrorCode::NETWORK_ERROR, "listener not bound");
  while (!stop_.load()) {
    TcpSocket sock;
    auto a = listener_.accept(sock, true);
    if (!a) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
    if (!a.value()) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
    auto ctx = std::make_shared<ConnContext>();
    ctx->socket = std::move(sock);
    {
      std::lock_guard lk(mu_);
      conns_.push_back(ctx);
    }
    threads_.emplace_back([this, ctx]() {
      try {
        handle_connection(std::move(ctx->socket));
      } catch (...) {
        // Thread exceptions must never crash the coordinator.
      }
    });
  }
  return Result<void>::success();
}

void Coordinator::handle_connection(TcpSocket sock) {
  std::optional<WorkerId> worker_id;
  std::optional<WorkerBootId> worker_boot;
  while (!stop_.load()) {
    auto f = read_frame(sock);
    if (!f) {
      if (f.code() == ErrorCode::PEER_LOST) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    auto r = handle_frame(sock, f.value(), worker_boot, worker_id);
    if (r.code() == ErrorCode::PEER_LOST) break;
  }
}

Result<void> Coordinator::handle_frame(TcpSocket& sock, const wire::Frame& f,
                                       std::optional<WorkerBootId>& wb,
                                       std::optional<WorkerId>& wid) {
  switch (f.kind) {
    case wire::MsgKind::MSG_REGISTER: {
      auto reg = wire::read_register(f.payload);
      if (!reg) return Result<void>::err(reg.code(), reg.message());
      WorkerId id(reg->worker);
      WorkerBootId boot(reg->boot);
      std::lock_guard lk(mu_);
      seq_++;
      workers_[id] = WorkerInfo{id, boot, HostGeneration(reg->host_gen), true, false, EpochMs(0)};
      wid = id;
      wb = boot;
      return reply_ack(sock, seq_, true, "registered");
    }
    case wire::MsgKind::MSG_INVENTORY:
    case wire::MsgKind::MSG_PATH_SNAPSHOT: {
      if (!wid || !wb) return Result<void>::err(ErrorCode::NOT_FOUND, "not registered");
      auto snap = persist::deserialize(f.payload);
      if (!snap) return Result<void>::err(snap.code(), snap.message());
      std::lock_guard lk(mu_);
      for (const auto& n : snap->nodes) {
        if (!topology_.contains(n.id)) {
          auto r = topology_.add_endpoint(n.id, n.bdf, n.kind, n.generation, n.provenance, n.parent ? *n.parent : PciNodeId(0));
          (void)r;
        }
      }
      for (const auto& p : snap->paths) {
        if (p.authority_matches(epoch_, *wb)) paths_[p.id()] = p;
      }
      return reply_ack(sock, seq_, true, "snapshot accepted");
    }
    case wire::MsgKind::MSG_MEASUREMENT: {
      if (!wb) return Result<void>::err(ErrorCode::NOT_FOUND, "not registered");
      Reader mr(f.payload);
      auto m = persist::decode_measurement(mr);
      if (!m) return Result<void>::err(m.code(), m.message());
      auto ing = measurements_.ingest(m.value());
      if (!ing) return reply_ack(sock, seq_, false, ing.message());
      return reply_ack(sock, seq_, true, "measurement accepted");
    }
    case wire::MsgKind::MSG_RESERVE: {
      if (!wb) return Result<void>::err(ErrorCode::NOT_FOUND, "not registered");
      Reader r(f.payload);
      auto res = persist::decode_reservation(r);
      if (!res) return Result<void>::err(res.code(), res.message());
      auto rr = reservations_.reserve(res.value());
      if (!rr) return reply_ack(sock, seq_, false, rr.message());
      return reply_ack(sock, seq_, true, "reserved");
    }
    case wire::MsgKind::MSG_RELEASE: {
      if (!wb) return Result<void>::err(ErrorCode::NOT_FOUND, "not registered");
      auto rel = wire::read_release(f.payload);
      if (!rel) return Result<void>::err(rel.code(), rel.message());
      auto rr = reservations_.release(ReservationId(rel->reservation), WorkerBootId(rel->boot),
                                      ReservationGeneration(rel->gen), CoordinatorEpoch(rel->epoch));
      if (!rr) return reply_ack(sock, seq_, false, rr.message());
      return reply_ack(sock, seq_, true, "released");
    }
    case wire::MsgKind::MSG_HEARTBEAT: {
      if (wid) { std::lock_guard lk(mu_); auto it = workers_.find(*wid); if (it != workers_.end()) it->second.last_seen = EpochMs(0); }
      return reply_ack(sock, seq_, true, "hb");
    }
    case wire::MsgKind::MSG_REVALIDATE: {
      if (wid) { std::lock_guard lk(mu_); auto it = workers_.find(*wid); if (it != workers_.end()) it->second.revalidation_required = true; }
      return reply_ack(sock, seq_, true, "revalidate");
    }
    case wire::MsgKind::MSG_POLICY_UPDATE: {
      return reply_ack(sock, seq_, true, "policy");
    }
    case wire::MsgKind::MSG_ACK: return Result<void>::success();
    case wire::MsgKind::MSG_ERROR: return Result<void>::success();
    case wire::MsgKind::MSG_HELLO: return reply_ack(sock, seq_, true, "hello");
    default: return Result<void>::err(ErrorCode::PROTOCOL_MALFORMED, "unexpected message");
  }
}

Result<void> Coordinator::reply_ack(TcpSocket& sock, std::uint64_t seqnum, bool ok,
                                    const std::string& msg) {
  Writer w; wire::write_ack(w, epoch_.value(), seqnum, ok, msg);
  wire::Frame f; f.kind = wire::MsgKind::MSG_ACK; f.payload = w.take();
  return write_frame(sock, f);
}

CoordinatorEpoch Coordinator::epoch() const { std::lock_guard lk(mu_); return epoch_; }
void Coordinator::advance_epoch() { std::lock_guard lk(mu_); epoch_ = CoordinatorEpoch(epoch_.value() + 1); }

std::size_t Coordinator::worker_count() const { std::lock_guard lk(mu_); return workers_.size(); }
std::size_t Coordinator::path_count() const { std::lock_guard lk(mu_); return paths_.size(); }
std::size_t Coordinator::reservation_active_count() const { return reservations_.active_count(); }
std::size_t Coordinator::measurement_count() const { return measurements_.size(); }
Result<void> Coordinator::validate_accounting() const { return reservations_.validate_accounting(); }

Result<void> Coordinator::revalidate_all() {
  std::lock_guard lk(mu_);
  for (auto& [k, p] : paths_) {
    FreshnessRecord f = p.freshness();
    if (f.state() != Freshness::INVALID) p.set_freshness(FreshnessRecord(Freshness::REVALIDATION_REQUIRED, f.age_ms()));
  }
  return Result<void>::success();
}

Snapshot Coordinator::snapshot() const {
  std::lock_guard lk(mu_);
  Snapshot s;
  s.epoch = epoch_;
  s.sequence = seq_;
  for (auto id : topology_.all_nodes()) {
    const Node* n = topology_.node(id);
    if (n) s.nodes.push_back(*n);
  }
  for (auto& [k, p] : paths_) s.paths.push_back(p);
  for (auto& m : measurements_.all()) s.measurements.push_back(m);
  s.semantic_digest = semantic_digest(s);
  return s;
}

Result<void> Coordinator::persist_now() const {
  if (persist_path_.empty()) return Result<void>::err(ErrorCode::NOT_FOUND, "no persist path");
  Snapshot s = snapshot();
  return persist::save(persist_path_, s);
}

}  // namespace pci