#pragma once
// PCIe link capability and optional negotiated state. Fields that cannot be
// obtained reliably are left as std::optional (i.e. UNKNOWN) rather than
// fabricated. Theoretical envelope is DERIVED; negotiated state is REPORTED or
// MEASURED where a backend truly exposes it.
#include <cstdint>
#include <optional>
#include <string>

#include "pciefabric/bandwidth.hpp"
#include "pciefabric/enums.hpp"
#include "pciefabric/ids.hpp"
#include "pciefabric/provenance.hpp"
#include "pciefabric/values.hpp"

namespace pci {

// Capability record for a PCIe link. Optional fields must be explicitly
// absent when a backend cannot supply them.
class LinkCapability {
public:
  LinkCapability() = default;

  [[nodiscard]] std::optional<PcieGen> supported_gen() const noexcept { return supported_gen_; }
  [[nodiscard]] std::optional<PcieGen> negotiated_gen() const noexcept { return negotiated_gen_; }
  [[nodiscard]] std::optional<std::uint16_t> supported_width() const noexcept { return supported_width_; }
  [[nodiscard]] std::optional<std::uint16_t> negotiated_width() const noexcept { return negotiated_width_; }
  [[nodiscard]] std::optional<Bytes> max_payload_size() const noexcept { return max_payload_; }
  [[nodiscard]] std::optional<Bytes> max_read_request_size() const noexcept { return max_rr_; }
  [[nodiscard]] std::optional<double> link_speed_gbps() const noexcept { return speed_gbps_; }
  [[nodiscard]] Provenance provenance() const noexcept { return provenance_; }

  void set_supported_gen(PcieGen g, Provenance p) { supported_gen_ = g; provenance_ = p; }
  void set_negotiated_gen(PcieGen g, Provenance p) { negotiated_gen_ = g; provenance_ = p; }
  void set_supported_width(std::uint16_t w) { supported_width_ = w; }
  void set_negotiated_width(std::uint16_t w) { negotiated_width_ = w; }
  void set_max_payload(Bytes b) { max_payload_ = b; }
  void set_max_rr(Bytes b) { max_rr_ = b; }
  void set_speed_gbps(double g) { speed_gbps_ = g; }
  void set_provenance(Provenance p) noexcept { provenance_ = p; }

  // Derive the theoretical envelope if enough capability is known as supported.
  [[nodiscard]] std::optional<LinkEnvelope> theoretical_envelope() const {
    if (!supported_gen_ || !supported_width_) return std::nullopt;
    LinkEnvelope e(*supported_gen_, LaneCount(static_cast<std::uint16_t>(*supported_width_)));
    return e.valid() ? std::optional<LinkEnvelope>(e) : std::nullopt;
  }

  // Negotiated (current) envelope, derived from the negotiated state only when
  // both negotiated generation and width are actually known.
  [[nodiscard]] std::optional<LinkEnvelope> negotiated_envelope() const {
    if (!negotiated_gen_ || !negotiated_width_) return std::nullopt;
    LinkEnvelope e(*negotiated_gen_, LaneCount(static_cast<std::uint16_t>(*negotiated_width_)));
    return e.valid() ? std::optional<LinkEnvelope>(e) : std::nullopt;
  }

private:
  std::optional<PcieGen> supported_gen_;
  std::optional<PcieGen> negotiated_gen_;
  std::optional<std::uint16_t> supported_width_;
  std::optional<std::uint16_t> negotiated_width_;
  std::optional<Bytes> max_payload_;
  std::optional<Bytes> max_rr_;
  std::optional<double> speed_gbps_;
  Provenance provenance_{Provenance::REPORTED};
};

// A link edge between two nodes. Each endpoint is identified by node id.
class Link {
public:
  Link() = default;
  Link(LinkId id, LinkGeneration gen, PciNodeId upstream, PciNodeId downstream,
       LinkCapability cap)
      : id_(id), generation_(gen), upstream_(upstream), downstream_(downstream),
        capability_(std::move(cap)) {}

  [[nodiscard]] LinkId id() const noexcept { return id_; }
  [[nodiscard]] LinkGeneration generation() const noexcept { return generation_; }
  [[nodiscard]] PciNodeId upstream() const noexcept { return upstream_; }
  [[nodiscard]] PciNodeId downstream() const noexcept { return downstream_; }
  [[nodiscard]] const LinkCapability& capability() const noexcept { return capability_; }
  [[nodiscard]] LinkCapability& capability() noexcept { return capability_; }
  void set_generation(LinkGeneration g) noexcept { generation_ = g; }

private:
  LinkId id_{};
  LinkGeneration generation_{};
  PciNodeId upstream_{};
  PciNodeId downstream_{};
  LinkCapability capability_;
};

}  // namespace pci
