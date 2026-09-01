#pragma once
// Versioned framed-TCP wire protocol. Every frame has magic + version +
// type + payload length + CRC-32. Malformed, truncated, oversize, stale-epoch,
// stale-boot, stale-generation, checksum-corrupt, and trailing-garbage frames
// are rejected. No unbounded allocation is possible from network-controlled
// lengths.
#include <cstdint>
#include <string>
#include <vector>

#include "pciefabric/codec.hpp"
#include "pciefabric/result.hpp"

namespace pci {
namespace wire {

inline constexpr char kMagic[4] = {'P', 'F', 'A', 'B'};
inline constexpr std::uint32_t kVersion = 1u;
inline constexpr std::size_t kMaxPayload = 16u * 1024u * 1024u;
inline constexpr std::size_t kHeaderSize = 4 + 4 + 1 + 8 + 4;

enum class MsgKind : std::uint8_t {
  MSG_HELLO = 1, MSG_REGISTER = 2, MSG_INVENTORY = 3, MSG_PATH_SNAPSHOT = 4, MSG_MEASUREMENT = 5,
  MSG_RESERVE = 6, MSG_RELEASE = 7, MSG_POLICY_UPDATE = 8, MSG_HEARTBEAT = 9, MSG_REVALIDATE = 10,
  MSG_ACK = 11, MSG_ERROR = 12
};

struct Frame {
  MsgKind kind{MsgKind::MSG_HELLO};
  std::vector<std::uint8_t> payload;
};

[[nodiscard]] inline std::vector<std::uint8_t> encode(const Frame& f) {
  Writer w;
  w.bytes(reinterpret_cast<const std::uint8_t*>(kMagic), 4);
  w.u32(kVersion);
  w.u8(static_cast<std::uint8_t>(f.kind));
  w.u64(static_cast<std::uint64_t>(f.payload.size()));
  w.u32(crc32(f.payload.data(), f.payload.size()));
  w.bytes(f.payload.data(), f.payload.size());
  return w.take();
}

// Decode one complete frame (header + payload concatenated) and validate it.
[[nodiscard]] inline Result<Frame> decode(const std::vector<std::uint8_t>& buf) {
  if (buf.size() < kHeaderSize)
    return Result<Frame>::err(ErrorCode::TRUNCATED, "frame too small");
  Reader r(buf);
  std::uint8_t mg[4];
  for (int i = 0; i < 4; ++i) { auto b = r.u8(); if (!b) return Result<Frame>::err(b.code(), b.message()); mg[i] = *b; }
  if (mg[0] != kMagic[0] || mg[1] != kMagic[1] || mg[2] != kMagic[2] || mg[3] != kMagic[3])
    return Result<Frame>::err(ErrorCode::PROTOCOL_MALFORMED, "bad magic");
  auto ver = r.u32(); if (!ver) return Result<Frame>::err(ver.code(), ver.message());
  if (*ver != kVersion) return Result<Frame>::err(ErrorCode::PROTOCOL_VERSION, "unsupported version");
  auto kind = r.u8(); if (!kind) return Result<Frame>::err(kind.code(), kind.message());
  if (*kind < 1 || *kind > 12) return Result<Frame>::err(ErrorCode::INVALID_ENUM, "invalid message kind");
  auto len = r.u64(); if (!len) return Result<Frame>::err(len.code(), len.message());
  if (*len > kMaxPayload) return Result<Frame>::err(ErrorCode::PROTOCOL_OVERFLOW, "payload too large");
  auto crc = r.u32(); if (!crc) return Result<Frame>::err(crc.code(), crc.message());
  if (buf.size() != kHeaderSize + static_cast<std::size_t>(*len))
    return Result<Frame>::err(ErrorCode::PROTOCOL_MALFORMED, "frame length mismatch");
  const std::uint8_t* payload = buf.data() + kHeaderSize;
  std::uint32_t computed = crc32(payload, static_cast<std::size_t>(*len));
  if (computed != *crc) return Result<Frame>::err(ErrorCode::PROTOCOL_CHECKSUM, "crc mismatch");
  Frame f;
  f.kind = static_cast<MsgKind>(*kind);
  f.payload.assign(payload, payload + (*len));
  return Result<Frame>::success(std::move(f));
}

struct RegisterRequest { std::uint64_t worker, boot, host_gen; };
inline void write_register(Writer& w, const RegisterRequest& reg) {
  w.u64(reg.worker); w.u64(reg.boot); w.u64(reg.host_gen);
}
[[nodiscard]] inline Result<RegisterRequest> read_register(const std::vector<std::uint8_t>& p) {
  Reader r(p);
  auto w = r.u64(); if (!w) return Result<RegisterRequest>::err(w.code(), w.message());
  auto b = r.u64(); if (!b) return Result<RegisterRequest>::err(b.code(), b.message());
  auto h = r.u64(); if (!h) return Result<RegisterRequest>::err(h.code(), h.message());
  auto e = r.require_end(); if (!e) return Result<RegisterRequest>::err(e.code(), e.message());
  return Result<RegisterRequest>::success(RegisterRequest{w.value(), b.value(), h.value()});
}

struct ReleaseRequest { std::uint64_t reservation, boot, gen, epoch; };
inline void write_release(Writer& w, const ReleaseRequest& rel) {
  w.u64(rel.reservation); w.u64(rel.boot); w.u64(rel.gen); w.u64(rel.epoch);
}
[[nodiscard]] inline Result<ReleaseRequest> read_release(const std::vector<std::uint8_t>& p) {
  Reader r(p);
  auto a = r.u64(); if (!a) return Result<ReleaseRequest>::err(a.code(), a.message());
  auto b = r.u64(); if (!b) return Result<ReleaseRequest>::err(b.code(), b.message());
  auto g = r.u64(); if (!g) return Result<ReleaseRequest>::err(g.code(), g.message());
  auto e = r.u64(); if (!e) return Result<ReleaseRequest>::err(e.code(), e.message());
  auto end = r.require_end(); if (!end) return Result<ReleaseRequest>::err(end.code(), end.message());
  return Result<ReleaseRequest>::success(ReleaseRequest{a.value(), b.value(), g.value(), e.value()});
}

struct Ack { std::uint64_t epoch, seq; bool ok; std::string message; };
inline void write_ack(Writer& w, std::uint64_t epoch, std::uint64_t seq, bool ok,
                      const std::string& msg) {
  w.u64(epoch); w.u64(seq); w.u8(ok ? 1 : 0); w.string(msg);
}
[[nodiscard]] inline Result<Ack> read_ack(const std::vector<std::uint8_t>& p) {
  Reader r(p);
  auto ep = r.u64(); if (!ep) return Result<Ack>::err(ep.code(), ep.message());
  auto sq = r.u64(); if (!sq) return Result<Ack>::err(sq.code(), sq.message());
  auto okb = r.u8(); if (!okb) return Result<Ack>::err(okb.code(), okb.message());
  auto m = r.string(); if (!m) return Result<Ack>::err(m.code(), m.message());
  auto end = r.require_end(); if (!end) return Result<Ack>::err(end.code(), end.message());
  return Result<Ack>::success(Ack{ep.value(), sq.value(), *okb != 0, *m});
}

}  // namespace wire
}  // namespace pci