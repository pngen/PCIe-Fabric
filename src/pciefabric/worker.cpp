// SPDX-License-Identifier: Apache-2.0
// Worker implementation. A fresh WorkerBootId is generated per process start.
#include "pciefabric/worker.hpp"

#include <chrono>
#include <cstring>

#include "pciefabric/persistence.hpp"
#include "pciefabric/tcp.hpp"

namespace pci {

Worker::Worker(WorkerId id, WorkerBootId boot, std::string host, std::uint16_t port,
               std::uint64_t host_gen)
    : id_(id), boot_(boot), host_(std::move(host)), port_(port), host_gen_(HostGeneration(host_gen)) {}
Worker::~Worker() { close(); }

Result<void> Worker::connect() {
  auto r = sock_.connect(host_, port_);
  if (!r) return r;
  connected_ = true;
  return Result<void>::success();
}

Result<void> Worker::register_self() {
  if (!connected_) return Result<void>::err(ErrorCode::NETWORK_ERROR, "not connected");
  wire::Frame f; f.kind = wire::MsgKind::MSG_REGISTER;
  Writer w; wire::write_register(w, wire::RegisterRequest{id_.value(), boot_.value(), host_gen_.value()});
  f.payload = w.take();
  auto a = send_and_await_ack(f);
  if (!a) return Result<void>::err(a.code(), a.message());
  epoch_ = CoordinatorEpoch(a->epoch);
  if (!a->ok) return Result<void>::err(ErrorCode::CONFLICT, a->message);
  return Result<void>::success();
}

Result<void> Worker::send_inventory(const Snapshot& snap) {
  auto body = persist::serialize(snap);
  if (!body) return Result<void>::err(body.code(), body.message());
  wire::Frame f; f.kind = wire::MsgKind::MSG_INVENTORY; f.payload = body.value();
  auto a = send_and_await_ack(f);
  if (!a) return Result<void>::err(a.code(), a.message());
  return a->ok ? Result<void>::success() : Result<void>::err(ErrorCode::CONFLICT, a->message);
}

Result<void> Worker::send_path_snapshot(const std::vector<Path>& paths, CoordinatorEpoch epoch,
                                        WorkerBootId boot) {
  (void)boot;
  Snapshot s; s.epoch = epoch; s.paths = paths;
  auto body = persist::serialize(s);
  if (!body) return Result<void>::err(body.code(), body.message());
  wire::Frame f; f.kind = wire::MsgKind::MSG_PATH_SNAPSHOT; f.payload = body.value();
  auto a = send_and_await_ack(f);
  if (!a) return Result<void>::err(a.code(), a.message());
  return a->ok ? Result<void>::success() : Result<void>::err(ErrorCode::CONFLICT, a->message);
}

Result<void> Worker::send_measurement(const Measurement& m) {
  Writer w; persist::encode(w, m);
  wire::Frame f; f.kind = wire::MsgKind::MSG_MEASUREMENT; f.payload = w.take();
  auto a = send_and_await_ack(f);
  if (!a) return Result<void>::err(a.code(), a.message());
  return a->ok ? Result<void>::success() : Result<void>::err(ErrorCode::CONFLICT, a->message);
}

Result<Reservation> Worker::reserve(const Reservation& proposed) {
  Writer w; persist::encode(w, proposed);
  wire::Frame f; f.kind = wire::MsgKind::MSG_RESERVE; f.payload = w.take();
  auto a = send_and_await_ack(f);
  if (!a) return Result<Reservation>::err(a.code(), a.message());
  if (!a->ok) return Result<Reservation>::err(ErrorCode::OVERCOMMIT, a->message);
  return Result<Reservation>::success(proposed);
}

Result<void> Worker::release(ReservationId id, ReservationGeneration gen) {
  wire::Frame f; f.kind = wire::MsgKind::MSG_RELEASE;
  Writer w; wire::write_release(w, wire::ReleaseRequest{id.value(), boot_.value(), gen.value(), epoch_.value()});
  f.payload = w.take();
  auto a = send_and_await_ack(f);
  if (!a) return Result<void>::err(a.code(), a.message());
  return a->ok ? Result<void>::success() : Result<void>::err(ErrorCode::CONFLICT, a->message);
}

Result<void> Worker::heartbeat() {
  wire::Frame f; f.kind = wire::MsgKind::MSG_HEARTBEAT;
  auto a = send_and_await_ack(f);
  if (!a) return Result<void>::err(a.code(), a.message());
  return Result<void>::success();
}

Result<void> Worker::revalidate() {
  wire::Frame f; f.kind = wire::MsgKind::MSG_REVALIDATE;
  auto a = send_and_await_ack(f);
  if (!a) return Result<void>::err(a.code(), a.message());
  return Result<void>::success();
}

Result<void> Worker::close() {
  if (connected_) { sock_.close(); connected_ = false; }
  return Result<void>::success();
}

Result<wire::Ack> Worker::send_and_await_ack(const wire::Frame& f) {
  auto w = write_frame(sock_, f);
  if (!w) return Result<wire::Ack>::err(w.code(), w.message());
  // Read frames until an ACK arrives (bounded loop).
  for (int i = 0; i < 64; ++i) {
    auto fr = read_frame(sock_);
    if (!fr) return Result<wire::Ack>::err(fr.code(), fr.message());
    if (fr->kind == wire::MsgKind::MSG_ACK) {
      return wire::read_ack(fr->payload);
    }
    if (fr->kind == wire::MsgKind::MSG_ERROR) return Result<wire::Ack>::err(ErrorCode::CONFLICT, "error frame");
    // ignore policy/revalidate pings during await
  }
  return Result<wire::Ack>::err(ErrorCode::TIMEOUT, "no ack");
}

}  // namespace pci