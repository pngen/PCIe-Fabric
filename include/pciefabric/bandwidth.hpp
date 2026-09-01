#pragma once
// Theoretical PCIe link envelope. All values are DERIVED, never measured.
// Units are kept strictly separate: GT/s (decimal) and GiB/s (binary 2^30) are
// never mixed. Effective payload accounts for encoding overhead (8b/10b for
// Gen1/2, 128b/130b for Gen3+). The result is an envelope, not throughput.
#include <array>
#include <cmath>
#include <cstdint>

#include "pciefabric/enums.hpp"
#include "pciefabric/values.hpp"

namespace pci {

namespace envelope {

// Signaling rate in gigatransfers per second for a generation (decimal).
[[nodiscard]] constexpr double signaling_gts(PcieGen g) noexcept {
  switch (g) {
    case PcieGen::GEN1: return 2.5;
    case PcieGen::GEN2: return 5.0;
    case PcieGen::GEN3: return 8.0;
    case PcieGen::GEN4: return 16.0;
    case PcieGen::GEN5: return 32.0;
    case PcieGen::GEN6: return 64.0;
    case PcieGen::UNKNOWN: return 0.0;
  }
  return 0.0;
}

// Effective encoding ratio (payload bits / link bits).
[[nodiscard]] constexpr double encoding_ratio(PcieGen g) noexcept {
  if (g == PcieGen::GEN1 || g == PcieGen::GEN2) return 8.0 / 10.0;       // 8b/10b
  return 128.0 / 130.0;                                                   // 128b/130b
}

// Effective payload Gbps per lane (signaling * encoding).
[[nodiscard]] constexpr double effective_gbps_per_lane(PcieGen g) noexcept {
  return signaling_gts(g) * encoding_ratio(g);
}

// Effective payload bytes/sec per lane.
[[nodiscard]] constexpr double effective_bytes_per_lane(PcieGen g) noexcept {
  return effective_gbps_per_lane(g) * 1.0e9 / 8.0;
}

// Raw (pre-encoding) signaling aggregate for a lane count, in bytes/sec.
[[nodiscard]] constexpr double raw_bytes_per_sec(PcieGen g, std::uint16_t lanes) noexcept {
  return signaling_gts(g) * 1.0e9 / 8.0 * static_cast<double>(lanes);
}

// Protocol-effective theoretical payload envelope for a link (bytes/sec).
[[nodiscard]] constexpr BytesPerSecond effective_bytes_per_sec(PcieGen g, std::uint16_t lanes) noexcept {
  return BytesPerSecond(effective_bytes_per_lane(g) * static_cast<double>(lanes));
}

// Same envelope expressed in binary GiB/s.
[[nodiscard]] constexpr GiBPerSecond effective_gib_per_sec(PcieGen g, std::uint16_t lanes) noexcept {
  return GiBPerSecond(effective_bytes_per_lane(g) * static_cast<double>(lanes) / (1024.0 * 1024.0 * 1024.0));
}

[[nodiscard]] inline bool lane_count_valid(std::uint16_t lanes) noexcept {
  return lanes >= 1 && lanes <= 32 && (lanes == 1 || lanes == 2 || lanes == 4 || lanes == 8 || lanes == 16 || lanes == 32);
}

}  // namespace envelope

// A fully derived theoretical link envelope. Provenance is DERIVED by construction.
class LinkEnvelope {
public:
  LinkEnvelope() = default;
  LinkEnvelope(PcieGen gen, LaneCount lanes)
      : generation_(gen), lanes_(lanes),
        effective_bps_(envelope::effective_bytes_per_sec(gen, lanes.value())),
        effective_gib_(envelope::effective_gib_per_sec(gen, lanes.value())),
        raw_bytes_(BytesPerSecond(envelope::raw_bytes_per_sec(gen, lanes.value()))) {}

  [[nodiscard]] PcieGen generation() const noexcept { return generation_; }
  [[nodiscard]] LaneCount lanes() const noexcept { return lanes_; }

  // Protocol-effective theoretical payload envelope.
  [[nodiscard]] BytesPerSecond effective_bytes_per_sec() const noexcept { return effective_bps_; }
  [[nodiscard]] GiBPerSecond effective_gib_per_sec() const noexcept { return effective_gib_; }
  // Raw signaling aggregate bytes/sec (pre-encoding).
  [[nodiscard]] BytesPerSecond raw_bytes_per_sec() const noexcept { return raw_bytes_; }

  [[nodiscard]] bool valid() const noexcept {
    return envelope::lane_count_valid(lanes_.value()) && effective_bps_.value() > 0.0;
  }
  [[nodiscard]] Provenance provenance() const noexcept { return Provenance::DERIVED; }

private:
  PcieGen generation_{PcieGen::UNKNOWN};
  LaneCount lanes_{};
  BytesPerSecond effective_bps_{};
  GiBPerSecond effective_gib_{};
  BytesPerSecond raw_bytes_{};
};

}  // namespace pci
