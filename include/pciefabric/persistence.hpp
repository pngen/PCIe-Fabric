#pragma once
// Deterministic versioned binary persistence: magic + version + bounded counts
// + CRC-32 + atomic temp->flush->close->rename. Corruption, truncation,
// duplicate-id, impossible-count, invalid-enum/generation/BDF, and trailing
// garbage are all rejected. Recovered physical observations are NOT current.
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <fstream>
#include <string>
#include <vector>

#include "pciefabric/codec.hpp"
#include "pciefabric/ids.hpp"
#include "pciefabric/result.hpp"
#include "pciefabric/snapshot.hpp"

namespace pci {
namespace persist {

inline constexpr char kMagic[4] = {'P', 'C', 'I', 'F'};
inline constexpr std::uint32_t kVersion = 1u;

// ---- encode ----
inline void encode(Writer& w, const Node& n) {
  w.string(n.bdf.canonical());
  w.u32(static_cast<std::uint32_t>(n.kind));
  w.u64(n.generation.value());
  w.u8(static_cast<std::uint8_t>(n.provenance));
  w.u8(n.parent ? 1 : 0);
  if (n.parent) w.u64(n.parent->value());
  w.u32(static_cast<std::uint32_t>(n.children.size()));
  for (auto c : n.children) w.u64(c.value());
  w.string(n.vendor); w.string(n.device); w.string(n.class_name); w.string(n.driver);
  w.u8(n.removed ? 1 : 0);
}

inline void encode(Writer& w, const Path& p) {
  w.u64(p.id().value()); w.u64(p.generation().value());
  w.u64(p.source().value()); w.u64(p.destination().value());
  w.u8(static_cast<std::uint8_t>(p.path_class()));
  w.u32(static_cast<std::uint32_t>(p.bridge_chain().size()));
  for (auto x : p.bridge_chain()) w.u64(x.value());
  w.u32(static_cast<std::uint32_t>(p.links().size()));
  for (auto x : p.links()) w.u64(x.value());
  w.u8(static_cast<std::uint8_t>(p.locality()));
  w.u8(p.theoretical() ? 1 : 0);
  if (p.theoretical()) {
    w.u8(static_cast<std::uint8_t>(p.theoretical()->generation()));
    w.u16(p.theoretical()->lanes().value());
  }
  w.u8(p.best_measured() ? 1 : 0);
  if (p.best_measured()) { w.f64(p.best_measured()->value()); w.u8(static_cast<std::uint8_t>(p.measured_provenance())); }
  w.u8(static_cast<std::uint8_t>(p.freshness().state()));
  w.u64(p.freshness().age_ms());
  w.u8(static_cast<std::uint8_t>(p.contention()));
  w.f64(p.contention_score().value());
  w.u8(static_cast<std::uint8_t>(p.confidence().value() * 255.0));
  w.u8(static_cast<std::uint8_t>(p.structural_provenance()));
  w.u64(p.epoch().value()); w.u64(p.boot().value());
}

inline void encode(Writer& w, const Measurement& m) {
  w.u64(m.id().value()); w.u64(m.generation().value());
  w.u64(m.source().value()); w.u64(m.destination().value());
  w.u8(static_cast<std::uint8_t>(m.transfer_class()));
  w.u8(static_cast<std::uint8_t>(m.engine()));
  w.u64(m.bytes().value()); w.u64(m.iterations().value());
  w.u64(m.duration_ns().value()); w.u64(m.latency_ns().value());
  w.u8(static_cast<std::uint8_t>(m.provenance()));
  w.u8(m.path() ? 1 : 0); if (m.path()) w.u64(m.path()->value());
  w.string(m.sync_semantics()); w.u64(m.warmup_ops()); w.string(m.backend_detail());
}

inline void encode(Writer& w, const Reservation& r) {
  w.u64(r.id().value()); w.u64(r.generation().value());
  w.u64(r.path().value()); w.u64(r.path_generation().value());
  w.u64(r.worker().value()); w.u64(r.boot().value()); w.u64(r.attempt().value());
  w.u64(r.epoch().value()); w.u64(r.policy().value()); w.u64(r.policy_generation().value());
  w.u64(r.bytes().value()); w.f64(r.bandwidth().value()); w.u64(r.duration().value());
  w.u32(r.priority()); w.u64(r.source().value()); w.u64(r.destination().value());
  w.string(r.workload()); w.u64(r.created_at().value());
  w.u8(r.active() ? 1 : 0); w.u8(r.released() ? 1 : 0);
}

// ---- decode ----
inline Result<Node> decode_node(Reader& r) {
  auto bdf = r.string(); if (!bdf) return Result<Node>::err(bdf.code(), bdf.message());
  auto pb = Bdf::parse(*bdf); if (!pb) return Result<Node>::err(ErrorCode::INVALID_BDF, "invalid node bdf");
  auto kind = r.u32(); if (!kind) return Result<Node>::err(kind.code(), kind.message());
  auto gen = r.u64(); if (!gen) return Result<Node>::err(gen.code(), gen.message());
  auto prov = r.u8(); if (!prov || !valid(static_cast<Provenance>(*prov))) return Result<Node>::err(ErrorCode::INVALID_ENUM, "invalid provenance");
  auto hasp = r.u8(); if (!hasp) return Result<Node>::err(hasp.code(), hasp.message());
  std::optional<PciNodeId> parent;
  if (*hasp) { auto p = r.u64(); if (!p) return Result<Node>::err(p.code(), p.message()); parent = PciNodeId(*p); }
  auto nchild = r.u32(); if (!nchild) return Result<Node>::err(nchild.code(), nchild.message());
  std::vector<PciNodeId> children;
  for (std::uint32_t i = 0; i < *nchild; ++i) { auto c = r.u64(); if (!c) return Result<Node>::err(c.code(), c.message()); children.push_back(PciNodeId(*c)); }
  Node n; (void)n;
  n.bdf = *pb; n.kind = static_cast<DeviceClass>(*kind);
  n.generation = PciNodeGeneration(*gen);
  n.provenance = static_cast<Provenance>(*prov); n.parent = parent; n.children = children;
  auto v = r.string(); if (!v) return Result<Node>::err(v.code(), v.message()); n.vendor = *v;
  auto d = r.string(); if (!d) return Result<Node>::err(d.code(), d.message()); n.device = *d;
  auto cl = r.string(); if (!cl) return Result<Node>::err(cl.code(), cl.message()); n.class_name = *cl;
  auto dr = r.string(); if (!dr) return Result<Node>::err(dr.code(), dr.message()); n.driver = *dr;
  auto rem = r.u8(); if (!rem) return Result<Node>::err(rem.code(), rem.message()); n.removed = *rem != 0;
  return Result<Node>::success(n);
}

inline Result<Path> decode_path(Reader& r) {
  auto id = r.u64(); if (!id) return Result<Path>::err(id.code(), id.message());
  auto gen = r.u64(); if (!gen) return Result<Path>::err(gen.code(), gen.message());
  Path p{PathId(*id), PathGeneration(*gen)};
  auto src = r.u64(); if (!src) return Result<Path>::err(src.code(), src.message());
  auto dst = r.u64(); if (!dst) return Result<Path>::err(dst.code(), dst.message());
  p.set_endpoints(PciNodeId(*src), PciNodeId(*dst));
  auto cl = r.u8(); if (!cl || !valid(static_cast<PathClass>(*cl))) return Result<Path>::err(ErrorCode::INVALID_ENUM, "invalid path class"); p.set_path_class(static_cast<PathClass>(*cl));
  auto nchain = r.u32(); if (!nchain) return Result<Path>::err(nchain.code(), nchain.message());
  std::vector<PciNodeId> chain; for (std::uint32_t i = 0; i < *nchain; ++i) { auto x = r.u64(); if (!x) return Result<Path>::err(x.code(), x.message()); chain.push_back(PciNodeId(*x)); } p.set_bridge_chain(chain);
  auto nlink = r.u32(); if (!nlink) return Result<Path>::err(nlink.code(), nlink.message());
  std::vector<LinkId> links; for (std::uint32_t i = 0; i < *nlink; ++i) { auto x = r.u64(); if (!x) return Result<Path>::err(x.code(), x.message()); links.push_back(LinkId(*x)); } p.set_links(links);
  auto loc = r.u8(); if (!loc || !valid(static_cast<LocalityClass>(*loc))) return Result<Path>::err(ErrorCode::INVALID_ENUM, "invalid locality"); p.set_locality(static_cast<LocalityClass>(*loc));
  auto hasth = r.u8(); if (!hasth) return Result<Path>::err(hasth.code(), hasth.message());
  if (*hasth) { auto g = r.u8(); if (!g) return Result<Path>::err(g.code(), g.message()); auto l = r.u16(); if (!l) return Result<Path>::err(l.code(), l.message()); p.set_theoretical(LinkEnvelope(static_cast<PcieGen>(*g), LaneCount(*l))); }
  auto hasm = r.u8(); if (!hasm) return Result<Path>::err(hasm.code(), hasm.message());
  if (*hasm) { auto bps = r.f64(); if (!bps) return Result<Path>::err(bps.code(), bps.message()); auto bp = r.u8(); if (!bp) return Result<Path>::err(bp.code(), bp.message()); p.set_best_measured(BytesPerSecond(*bps), static_cast<Provenance>(*bp)); }
  auto f = r.u8(); if (!f || !valid(static_cast<Freshness>(*f))) return Result<Path>::err(ErrorCode::INVALID_ENUM, "invalid freshness"); auto age = r.u64(); if (!age) return Result<Path>::err(age.code(), age.message()); p.set_freshness(FreshnessRecord(static_cast<Freshness>(*f), *age));
  auto cont = r.u8(); if (!cont || !valid(static_cast<ContentionState>(*cont))) return Result<Path>::err(ErrorCode::INVALID_ENUM, "invalid contention"); p.set_contention(static_cast<ContentionState>(*cont));
  auto cs = r.f64(); if (!cs) return Result<Path>::err(cs.code(), cs.message()); p.set_contention_score(ContentionScore(*cs));
  auto conf = r.u8(); if (!conf) return Result<Path>::err(conf.code(), conf.message()); p.set_confidence(Confidence((*conf) / 255.0));
  auto sp = r.u8(); if (!sp) return Result<Path>::err(sp.code(), sp.message()); p.set_structural_provenance(static_cast<Provenance>(*sp));
  auto ep = r.u64(); if (!ep) return Result<Path>::err(ep.code(), ep.message()); auto bp2 = r.u64(); if (!bp2) return Result<Path>::err(bp2.code(), bp2.message());
  p.set_authority(CoordinatorEpoch(*ep), WorkerBootId(*bp2), HostGeneration(1), p.generation(), DeviceGeneration(1));
  return Result<Path>::success(p);
}

inline Result<Measurement> decode_measurement(Reader& r) {
  auto id = r.u64(); if (!id) return Result<Measurement>::err(id.code(), id.message());
  auto gen = r.u64(); if (!gen) return Result<Measurement>::err(gen.code(), gen.message());
  auto src = r.u64(); if (!src) return Result<Measurement>::err(src.code(), src.message());
  auto dst = r.u64(); if (!dst) return Result<Measurement>::err(dst.code(), dst.message());
  auto tc = r.u8(); if (!tc || !valid(static_cast<TransferClass>(*tc))) return Result<Measurement>::err(ErrorCode::INVALID_ENUM, "invalid transfer class");
  auto eng = r.u8(); if (!eng || !valid(static_cast<EngineKind>(*eng))) return Result<Measurement>::err(ErrorCode::INVALID_ENUM, "invalid engine");
  auto bytes = r.u64(); if (!bytes) return Result<Measurement>::err(bytes.code(), bytes.message());
  auto iters = r.u64(); if (!iters) return Result<Measurement>::err(iters.code(), iters.message());
  auto dur = r.u64(); if (!dur) return Result<Measurement>::err(dur.code(), dur.message());
  auto lat = r.u64(); if (!lat) return Result<Measurement>::err(lat.code(), lat.message());
  auto prov = r.u8(); if (!prov || !valid(static_cast<Provenance>(*prov))) return Result<Measurement>::err(ErrorCode::INVALID_ENUM, "invalid provenance");
  Measurement m(MeasurementId(*id), MeasurementGeneration(*gen), PciNodeId(*src), PciNodeId(*dst),
                static_cast<TransferClass>(*tc), static_cast<EngineKind>(*eng),
                TransferBytes(*bytes), IterationCount(*iters), Nanoseconds(*dur), static_cast<Provenance>(*prov));
  m.set_latency(Nanoseconds(*lat));
  auto hasp = r.u8(); if (!hasp) return Result<Measurement>::err(hasp.code(), hasp.message());
  if (*hasp) { auto pid = r.u64(); if (!pid) return Result<Measurement>::err(pid.code(), pid.message()); m.set_path(PathId(*pid), PathGeneration(1)); }
  auto ss = r.string(); if (!ss) return Result<Measurement>::err(ss.code(), ss.message()); m.set_sync_semantics(*ss);
  auto wo = r.u64(); if (!wo) return Result<Measurement>::err(wo.code(), wo.message()); m.set_warmup_ops(*wo);
  auto bd = r.string(); if (!bd) return Result<Measurement>::err(bd.code(), bd.message()); m.set_backend_detail(*bd);
  return Result<Measurement>::success(m);
}

inline Result<Reservation> decode_reservation(Reader& r) {
  auto id = r.u64(); if (!id) return Result<Reservation>::err(id.code(), id.message());
  auto gen = r.u64(); if (!gen) return Result<Reservation>::err(gen.code(), gen.message());
  Reservation res{ReservationId(*id), ReservationGeneration(*gen)};
  auto path = r.u64(); if (!path) return Result<Reservation>::err(path.code(), path.message());
  auto pgen = r.u64(); if (!pgen) return Result<Reservation>::err(pgen.code(), pgen.message());
  res.set_path(PathId(*path), PathGeneration(*pgen));
  auto w = r.u64(); if (!w) return Result<Reservation>::err(w.code(), w.message());
  auto boot = r.u64(); if (!boot) return Result<Reservation>::err(boot.code(), boot.message());
  auto att = r.u64(); if (!att) return Result<Reservation>::err(att.code(), att.message());
  auto ep = r.u64(); if (!ep) return Result<Reservation>::err(ep.code(), ep.message());
  auto pol = r.u64(); if (!pol) return Result<Reservation>::err(pol.code(), pol.message());
  auto pgen2 = r.u64(); if (!pgen2) return Result<Reservation>::err(pgen2.code(), pgen2.message());
  res.set_authority(WorkerId(*w), WorkerBootId(*boot), AttemptId(*att), CoordinatorEpoch(*ep), PolicyId(*pol), PolicyGeneration(*pgen2));
  auto bytes = r.u64(); if (!bytes) return Result<Reservation>::err(bytes.code(), bytes.message());
  auto bw = r.f64(); if (!bw) return Result<Reservation>::err(bw.code(), bw.message());
  auto dur = r.u64(); if (!dur) return Result<Reservation>::err(dur.code(), dur.message());
  auto prio = r.u32(); if (!prio) return Result<Reservation>::err(prio.code(), prio.message());
  auto src = r.u64(); if (!src) return Result<Reservation>::err(src.code(), src.message());
  auto dst = r.u64(); if (!dst) return Result<Reservation>::err(dst.code(), dst.message());
  auto wk = r.string(); if (!wk) return Result<Reservation>::err(wk.code(), wk.message());
  auto created = r.u64(); if (!created) return Result<Reservation>::err(created.code(), created.message());
  res.set_request(CapacityBytes(*bytes), BytesPerSecond(*bw), Milliseconds(*dur), *prio, PciNodeId(*src), PciNodeId(*dst), *wk);
  auto act = r.u8(); if (!act) return Result<Reservation>::err(act.code(), act.message());
  auto rel = r.u8(); if (!rel) return Result<Reservation>::err(rel.code(), rel.message());
  if (*act) res.mark_active(); else res.mark_released();
  (void)created;
  return Result<Reservation>::success(res);
}

// ---- whole file ----
inline Result<std::vector<std::uint8_t>> serialize(const Snapshot& s) {
  Writer w;
  w.string("PCIEFABRIC");
  w.u64(s.sequence); w.u64(s.epoch.value());
  w.u32_count(s.nodes.size());
  for (const auto& n : s.nodes) encode(w, n);
  w.u32_count(s.paths.size());
  for (const auto& p : s.paths) encode(w, p);
  w.u32_count(s.measurements.size());
  for (const auto& m : s.measurements) encode(w, m);
  w.u32_count(s.reservations.size());
  for (const auto& r : s.reservations) encode(w, r);
  w.u32_count(s.policies.size());
  for (const auto& p : s.policies) w.u64(p.value());
  return Result<std::vector<std::uint8_t>>::success(w.take());
}

inline Result<Snapshot> deserialize(const std::vector<std::uint8_t>& body) {
  Reader r(body);
  Snapshot s;
  auto magic = r.string(); if (!magic) return Result<Snapshot>::err(magic.code(), magic.message());
  if (*magic != "PCIEFABRIC") return Result<Snapshot>::err(ErrorCode::PROTOCOL_MALFORMED, "bad stream magic");
  auto seq = r.u64(); if (!seq) return Result<Snapshot>::err(seq.code(), seq.message()); s.sequence = *seq;
  auto ep = r.u64(); if (!ep) return Result<Snapshot>::err(ep.code(), ep.message()); s.epoch = CoordinatorEpoch(*ep);
  auto nn = r.u32_count(); if (!nn) return Result<Snapshot>::err(nn.code(), nn.message());
  // node ids must be unique
  std::map<PciNodeId, int> seen;
  for (std::uint32_t i = 0; i < *nn; ++i) {
    auto n = decode_node(r); if (!n) return Result<Snapshot>::err(n.code(), n.message());
    if (seen[n.value().id]++) return Result<Snapshot>::err(ErrorCode::DUPLICATE_IDENTITY, "duplicate node id");
    s.nodes.push_back(std::move(n.value()));
  }
  auto np = r.u32_count(); if (!np) return Result<Snapshot>::err(np.code(), np.message());
  std::set<PathId> pseen;
  for (std::uint32_t i = 0; i < *np; ++i) { auto p = decode_path(r); if (!p) return Result<Snapshot>::err(p.code(), p.message()); if (pseen.contains(p.value().id())) return Result<Snapshot>::err(ErrorCode::DUPLICATE_IDENTITY, "duplicate path id"); pseen.insert(p.value().id()); s.paths.push_back(std::move(p.value())); }
  auto nm = r.u32_count(); if (!nm) return Result<Snapshot>::err(nm.code(), nm.message());
  std::set<MeasurementId> mseen;
  for (std::uint32_t i = 0; i < *nm; ++i) { auto m = decode_measurement(r); if (!m) return Result<Snapshot>::err(m.code(), m.message()); if (mseen.contains(m.value().id())) return Result<Snapshot>::err(ErrorCode::DUPLICATE_IDENTITY, "duplicate measurement id"); mseen.insert(m.value().id()); s.measurements.push_back(std::move(m.value())); }
  auto nr = r.u32_count(); if (!nr) return Result<Snapshot>::err(nr.code(), nr.message());
  std::set<ReservationId> rseen;
  for (std::uint32_t i = 0; i < *nr; ++i) { auto res = decode_reservation(r); if (!res) return Result<Snapshot>::err(res.code(), res.message()); if (rseen.contains(res.value().id())) return Result<Snapshot>::err(ErrorCode::DUPLICATE_IDENTITY, "duplicate reservation id"); rseen.insert(res.value().id()); s.reservations.push_back(std::move(res.value())); }
  auto npol = r.u32_count(); if (!npol) return Result<Snapshot>::err(npol.code(), npol.message());
  for (std::uint32_t i = 0; i < *npol; ++i) { auto p = r.u64(); if (!p) return Result<Snapshot>::err(p.code(), p.message()); s.policies.push_back(PolicyId(*p)); }
  auto end = r.require_end(); if (!end) return Result<Snapshot>::err(end.code(), end.message());
  s.semantic_digest = semantic_digest(s);
  return Result<Snapshot>::success(std::move(s));
}

// Atomic temp -> flush -> close -> rename.
inline Result<void> save(const std::string& path, const Snapshot& s) {
  auto body = serialize(s); if (!body) return Result<void>::err(body.code(), body.message());
  Writer w;
  w.bytes(reinterpret_cast<const std::uint8_t*>(kMagic), 4);
  w.u32(kVersion);
  w.u64(static_cast<std::uint64_t>((*body).size()));
  w.u32(crc32((*body).data(), (*body).size()));
  w.bytes((*body).data(), (*body).size());
  auto bytes = w.take();

  std::filesystem::path p(path);
  std::filesystem::path tmp = p; tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return Result<void>::err(ErrorCode::IO_ERROR, "cannot open temp file");
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    f.flush();
    if (!f) return Result<void>::err(ErrorCode::IO_ERROR, "write failed");
  }
  std::error_code ec;
  if (std::filesystem::exists(p)) {
    std::filesystem::remove(p, ec);
    if (ec) return Result<void>::err(ErrorCode::IO_ERROR, "cannot remove old file");
  }
  std::filesystem::rename(tmp, p, ec);
  if (ec) return Result<void>::err(ErrorCode::IO_ERROR, "rename failed");
  return Result<void>::success();
}

inline Result<Snapshot> load(const std::string& path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return Result<Snapshot>::err(ErrorCode::IO_ERROR, "cannot open file");
  auto size = f.tellg();
  if (size < 16) return Result<Snapshot>::err(ErrorCode::TRUNCATED, "file too small");
  std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
  f.seekg(0); f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
  if (!f) return Result<Snapshot>::err(ErrorCode::IO_ERROR, "read failed");
  Reader r(data);
  std::uint8_t magic[4];
  for (int i = 0; i < 4; ++i) { auto b = r.u8(); if (!b) return Result<Snapshot>::err(b.code(), b.message()); magic[i] = *b; }
  if (magic[0] != kMagic[0] || magic[1] != kMagic[1] || magic[2] != kMagic[2] || magic[3] != kMagic[3])
    return Result<Snapshot>::err(ErrorCode::PROTOCOL_MALFORMED, "bad file magic");
  auto ver = r.u32(); if (!ver) return Result<Snapshot>::err(ver.code(), ver.message());
  if (*ver != kVersion) return Result<Snapshot>::err(ErrorCode::PROTOCOL_VERSION, "unsupported version");
  auto len = r.u64(); if (!len) return Result<Snapshot>::err(len.code(), len.message());
  if (*len > data.size()) return Result<Snapshot>::err(ErrorCode::PROTOCOL_OVERFLOW, "length out of bounds");
  auto crc = r.u32(); if (!crc) return Result<Snapshot>::err(crc.code(), crc.message());
  std::vector<std::uint8_t> body(data.end() - static_cast<std::ptrdiff_t>(*len), data.end());
  std::uint32_t computed = crc32(body.data(), body.size());
  if (computed != *crc) return Result<Snapshot>::err(ErrorCode::PROTOCOL_CHECKSUM, "crc mismatch");
  return deserialize(body);
}

// On recovery, recovered physical observations must not be CURRENT. This
// downgrades physical facts to REVALIDATION_REQUIRED.
inline void mark_physical_revalidation_required(Snapshot& s) {
  for (auto& p : s.paths) {
    FreshnessRecord f = p.freshness();
    if (f.state() != Freshness::INVALID) p.set_freshness(FreshnessRecord(Freshness::REVALIDATION_REQUIRED, f.age_ms()));
  }
  // Nodes / measurements remain as durable evidence but must be revalidated by
  // a live worker before re-entering CURRENT authority.
}

}  // namespace persist
}  // namespace pci