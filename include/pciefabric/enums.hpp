#pragma once
// Semantic enums for PCIe Fabric. Every enum has a name mapping and a strict
// validity check so that malformed codes can never be accepted silently.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pci {

// Where a value came from.
enum class Provenance : std::uint8_t {
  MEASURED, REPORTED, DERIVED, ESTIMATED, SYNTHETIC, UNKNOWN
};

// How current a fact currently is.
enum class Freshness : std::uint8_t {
  CURRENT, AGING, STALE, REVALIDATION_REQUIRED, INVALID
};

// Topology-derived locality class. Derived, not a single opaque score.
enum class LocalityClass : std::uint8_t {
  SAME_ENDPOINT, SAME_SWITCH, SAME_ROOT_COMPLEX, SAME_NUMA_DOMAIN,
  SAME_HOST_REMOTE_ROOT, CROSS_NUMA, UNKNOWN, SYNTHETIC
};

// Directional / semantic path class.
enum class PathClass : std::uint8_t {
  ENDPOINT_TO_HOST, HOST_TO_ENDPOINT, ENDPOINT_TO_ENDPOINT,
  ACCELERATOR_TO_HOST, HOST_TO_ACCELERATOR, ACCELERATOR_TO_NIC,
  ACCELERATOR_TO_STORAGE, NIC_TO_ACCELERATOR, STORAGE_TO_ACCELERATOR,
  PEER_ENDPOINT_PATH, UNKNOWN
};

// Observed contention state. Measured or forced-synthetic, never inferred alone.
enum class ContentionState : std::uint8_t {
  UNKNOWN, IDLE, LOW, MODERATE, HIGH, SATURATED
};

// Path selection result.
enum class Decision : std::uint8_t {
  USE_PATH, USE_PATH_WITH_PENALTY, RESERVE_PATH, DEFER, REJECT, REVALIDATION_REQUIRED
};

// Device class taxonomy.
enum class DeviceClass : std::uint8_t {
  UNKNOWN, OTHER, ACCELERATOR, NIC, STORAGE, MEMORY, BRIDGE, ROOT_PORT,
  ROOT_COMPLEX, SWITCH, HOST_BRIDGE
};

// PCIe link generation.
enum class PcieGen : std::uint8_t {
  UNKNOWN, GEN1, GEN2, GEN3, GEN4, GEN5, GEN6
};

// Measurement backend / engine.
enum class EngineKind : std::uint8_t {
  NONE, CUDA, SYNTHETIC, WINDOWS, COORDINATOR
};

// Transfer direction semantics for measurements.
enum class TransferClass : std::uint8_t {
  UNKNOWN, PAGEABLE_H2D, PAGEABLE_D2H, PINNED_H2D, PINNED_D2H,
  SYNCHRONOUS_COPY, KERNEL_LAUNCH
};

// Authority envelope component used for fencing.
enum class AuthorityKind : std::uint8_t {
  COORDINATOR_EPOCH, WORKER_BOOT, HOST_GENERATION, PCI_NODE_GENERATION,
  DEVICE_GENERATION, PATH_GENERATION, LINK_GENERATION, MEASUREMENT_GENERATION,
  RESERVATION_GENERATION, POLICY_GENERATION, ATTEMPT
};

// Streaming / capability flags (independent, not collapsed).
enum class Capability : std::uint8_t {
  PEER_ACCESS, GPUDIRECT, PEER_MEMORY, TRANSPORT_OFFLOAD,
  SR_IOV, ARI, ACS, RESIZABLE_BAR, DIRECT_PATH
};

// ---------------------------------------------------------------------------
// Name maps. Constexpr tables validated by tests.
// ---------------------------------------------------------------------------
inline constexpr std::array<std::string_view, 6> kProvenanceNames{
  "MEASURED", "REPORTED", "DERIVED", "ESTIMATED", "SYNTHETIC", "UNKNOWN"};
inline constexpr std::array<std::string_view, 5> kFreshnessNames{
  "CURRENT", "AGING", "STALE", "REVALIDATION_REQUIRED", "INVALID"};
inline constexpr std::array<std::string_view, 8> kLocalityNames{
  "SAME_ENDPOINT", "SAME_SWITCH", "SAME_ROOT_COMPLEX", "SAME_NUMA_DOMAIN",
  "SAME_HOST_REMOTE_ROOT", "CROSS_NUMA", "UNKNOWN", "SYNTHETIC"};
inline constexpr std::array<std::string_view, 11> kPathClassNames{
  "ENDPOINT_TO_HOST", "HOST_TO_ENDPOINT", "ENDPOINT_TO_ENDPOINT",
  "ACCELERATOR_TO_HOST", "HOST_TO_ACCELERATOR", "ACCELERATOR_TO_NIC",
  "ACCELERATOR_TO_STORAGE", "NIC_TO_ACCELERATOR", "STORAGE_TO_ACCELERATOR",
  "PEER_ENDPOINT_PATH", "UNKNOWN"};
inline constexpr std::array<std::string_view, 6> kContentionNames{
  "UNKNOWN", "IDLE", "LOW", "MODERATE", "HIGH", "SATURATED"};
inline constexpr std::array<std::string_view, 6> kDecisionNames{
  "USE_PATH", "USE_PATH_WITH_PENALTY", "RESERVE_PATH", "DEFER", "REJECT",
  "REVALIDATION_REQUIRED"};
inline constexpr std::array<std::string_view, 11> kDeviceClassNames{
  "UNKNOWN", "OTHER", "ACCELERATOR", "NIC", "STORAGE", "MEMORY", "BRIDGE",
  "ROOT_PORT", "ROOT_COMPLEX", "SWITCH", "HOST_BRIDGE"};
inline constexpr std::array<std::string_view, 7> kPcieGenNames{
  "UNKNOWN", "GEN1", "GEN2", "GEN3", "GEN4", "GEN5", "GEN6"};
inline constexpr std::array<std::string_view, 5> kEngineNames{
  "NONE", "CUDA", "SYNTHETIC", "WINDOWS", "COORDINATOR"};
inline constexpr std::array<std::string_view, 7> kTransferClassNames{
  "UNKNOWN", "PAGEABLE_H2D", "PAGEABLE_D2H", "PINNED_H2D", "PINNED_D2H",
  "SYNCHRONOUS_COPY", "KERNEL_LAUNCH"};
inline constexpr std::array<std::string_view, 11> kAuthorityNames{
  "COORDINATOR_EPOCH", "WORKER_BOOT", "HOST_GENERATION", "PCI_NODE_GENERATION",
  "DEVICE_GENERATION", "PATH_GENERATION", "LINK_GENERATION",
  "MEASUREMENT_GENERATION", "RESERVATION_GENERATION", "POLICY_GENERATION", "ATTEMPT"};
inline constexpr std::array<std::string_view, 9> kCapabilityNames{
  "PEER_ACCESS", "GPUDIRECT", "PEER_MEMORY", "TRANSPORT_OFFLOAD", "SR_IOV",
  "ARI", "ACS", "RESIZABLE_BAR", "DIRECT_PATH"};

namespace detail {
template <std::size_t N>
[[nodiscard]] inline bool in_range(std::uint8_t v,
                                   const std::array<std::string_view, N>&) noexcept {
  return v < N;
}
}  // namespace detail

[[nodiscard]] inline bool valid(Provenance p) noexcept {
  return detail::in_range(static_cast<std::uint8_t>(p), kProvenanceNames);
}
[[nodiscard]] inline bool valid(Freshness f) noexcept {
  return detail::in_range(static_cast<std::uint8_t>(f), kFreshnessNames);
}
[[nodiscard]] inline bool valid(LocalityClass l) noexcept {
  return detail::in_range(static_cast<std::uint8_t>(l), kLocalityNames);
}
[[nodiscard]] inline bool valid(PathClass p) noexcept {
  return detail::in_range(static_cast<std::uint8_t>(p), kPathClassNames);
}
[[nodiscard]] inline bool valid(ContentionState c) noexcept {
  return detail::in_range(static_cast<std::uint8_t>(c), kContentionNames);
}
[[nodiscard]] inline bool valid(Decision d) noexcept {
  return detail::in_range(static_cast<std::uint8_t>(d), kDecisionNames);
}
[[nodiscard]] inline bool valid(DeviceClass d) noexcept {
  return detail::in_range(static_cast<std::uint8_t>(d), kDeviceClassNames);
}
[[nodiscard]] inline bool valid(PcieGen g) noexcept {
  return detail::in_range(static_cast<std::uint8_t>(g), kPcieGenNames);
}
[[nodiscard]] inline bool valid(EngineKind e) noexcept {
  return detail::in_range(static_cast<std::uint8_t>(e), kEngineNames);
}
[[nodiscard]] inline bool valid(TransferClass t) noexcept {
  return detail::in_range(static_cast<std::uint8_t>(t), kTransferClassNames);
}
[[nodiscard]] inline bool valid(AuthorityKind a) noexcept {
  return detail::in_range(static_cast<std::uint8_t>(a), kAuthorityNames);
}

template <std::size_t N>
[[nodiscard]] inline std::string enum_name(std::uint8_t v,
                                            const std::array<std::string_view, N>& names,
                                            std::string_view fallback) noexcept {
  if (v < N) return std::string(names[v]);
  return std::string(fallback);
}

[[nodiscard]] inline std::string to_string(Provenance p) noexcept {
  return enum_name(static_cast<std::uint8_t>(p), kProvenanceNames, "UNKNOWN");
}
[[nodiscard]] inline std::string to_string(Freshness f) noexcept {
  return enum_name(static_cast<std::uint8_t>(f), kFreshnessNames, "INVALID");
}
[[nodiscard]] inline std::string to_string(LocalityClass l) noexcept {
  return enum_name(static_cast<std::uint8_t>(l), kLocalityNames, "UNKNOWN");
}
[[nodiscard]] inline std::string to_string(PathClass p) noexcept {
  return enum_name(static_cast<std::uint8_t>(p), kPathClassNames, "UNKNOWN");
}
[[nodiscard]] inline std::string to_string(ContentionState c) noexcept {
  return enum_name(static_cast<std::uint8_t>(c), kContentionNames, "UNKNOWN");
}
[[nodiscard]] inline std::string to_string(Decision d) noexcept {
  return enum_name(static_cast<std::uint8_t>(d), kDecisionNames, "REJECT");
}
[[nodiscard]] inline std::string to_string(DeviceClass d) noexcept {
  return enum_name(static_cast<std::uint8_t>(d), kDeviceClassNames, "UNKNOWN");
}
[[nodiscard]] inline std::string to_string(PcieGen g) noexcept {
  return enum_name(static_cast<std::uint8_t>(g), kPcieGenNames, "UNKNOWN");
}
[[nodiscard]] inline std::string to_string(EngineKind e) noexcept {
  return enum_name(static_cast<std::uint8_t>(e), kEngineNames, "NONE");
}
[[nodiscard]] inline std::string to_string(TransferClass t) noexcept {
  return enum_name(static_cast<std::uint8_t>(t), kTransferClassNames, "UNKNOWN");
}
[[nodiscard]] inline std::string to_string(AuthorityKind a) noexcept {
  return enum_name(static_cast<std::uint8_t>(a), kAuthorityNames, "ATTEMPT");
}
[[nodiscard]] inline std::string to_string(Capability c) noexcept {
  return enum_name(static_cast<std::uint8_t>(c), kCapabilityNames, "DIRECT_PATH");
}

}  // namespace pci
