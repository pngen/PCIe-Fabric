#pragma once
// Strong, distinct typed identities and generations for PCIe Fabric 1.0.0.
// Identity (who) and mutable generation (how current) are kept separate.
#include <compare>
#include <cstdint>
#include <functional>
#include <string>

namespace pci {

template <typename Tag, typename Underlying = std::uint64_t>
class StrongId {
public:
  using value_type = Underlying;
  constexpr StrongId() noexcept = default;
  explicit constexpr StrongId(Underlying value) noexcept : value_(value) {}
  [[nodiscard]] constexpr bool is_null() const noexcept { return value_ == Underlying{0}; }
  [[nodiscard]] constexpr bool valid() const noexcept { return !is_null(); }
  [[nodiscard]] constexpr Underlying value() const noexcept { return value_; }
  explicit constexpr operator bool() const noexcept { return valid(); }
  friend constexpr bool operator==(StrongId, StrongId) noexcept = default;
  friend constexpr std::strong_ordering operator<=>(StrongId a, StrongId b) noexcept {
    return a.value() <=> b.value();
  }
private:
  Underlying value_{};
};

template <typename Tag, typename U>
inline std::string to_string(const StrongId<Tag, U>& id) { return std::to_string(id.value()); }

template <typename Tag>
class Generation {
public:
  using value_type = std::uint64_t;
  constexpr Generation() noexcept = default;
  explicit constexpr Generation(std::uint64_t value) noexcept : value_(value) {}
  [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }
  [[nodiscard]] constexpr bool is_zero() const noexcept { return value_ == 0; }
  constexpr Generation& operator++() noexcept { ++value_; return *this; }
  constexpr Generation operator++(int) noexcept { Generation t{*this}; ++value_; return t; }
  friend constexpr bool operator==(Generation, Generation) noexcept = default;
  friend constexpr std::strong_ordering operator<=>(Generation a, Generation b) noexcept {
    return a.value() <=> b.value();
  }
private:
  std::uint64_t value_{};
};

struct HostIdTag {};            struct PciDomainIdTag {};     struct PciBusIdTag {};
struct PciDeviceIdTag {};       struct PciFunctionIdTag {};   struct PciBdfTag {};
struct PciNodeIdTag {};         struct RootComplexIdTag {};   struct BridgeIdTag {};
struct EndpointIdTag {};        struct AcceleratorIdTag {};   struct NicIdTag {};
struct StorageDeviceIdTag {};   struct MemoryDeviceIdTag {};  struct PathIdTag {};
struct LinkIdTag {};            struct MeasurementIdTag {};   struct ReservationIdTag {};
struct PolicyIdTag {};          struct WorkerIdTag {};        struct WorkerBootIdTag {};
struct CoordinatorEpochTag {};  struct ObservationIdTag {};   struct AttemptIdTag {};
struct SourceIdTag {};          struct SnapshotIdTag {};

using HostId = StrongId<HostIdTag>;
using PciDomainId = StrongId<PciDomainIdTag>;
using PciBusId = StrongId<PciBusIdTag, std::uint32_t>;
using PciDeviceId = StrongId<PciDeviceIdTag, std::uint32_t>;
using PciFunctionId = StrongId<PciFunctionIdTag, std::uint32_t>;
using PciBdf = StrongId<PciBdfTag>;
using PciNodeId = StrongId<PciNodeIdTag>;
using RootComplexId = StrongId<RootComplexIdTag>;
using BridgeId = StrongId<BridgeIdTag>;
using EndpointId = StrongId<EndpointIdTag>;
using AcceleratorId = StrongId<AcceleratorIdTag>;
using NicId = StrongId<NicIdTag>;
using StorageDeviceId = StrongId<StorageDeviceIdTag>;
using MemoryDeviceId = StrongId<MemoryDeviceIdTag>;
using PathId = StrongId<PathIdTag>;
using LinkId = StrongId<LinkIdTag>;
using MeasurementId = StrongId<MeasurementIdTag>;
using ReservationId = StrongId<ReservationIdTag>;
using PolicyId = StrongId<PolicyIdTag>;
using WorkerId = StrongId<WorkerIdTag>;
using WorkerBootId = StrongId<WorkerBootIdTag>;
using CoordinatorEpoch = StrongId<CoordinatorEpochTag>;
using ObservationId = StrongId<ObservationIdTag>;
using AttemptId = StrongId<AttemptIdTag>;
using SourceId = StrongId<SourceIdTag>;
using SnapshotId = StrongId<SnapshotIdTag>;

struct HostGenerationTag {};        struct PciNodeGenerationTag {};
struct DeviceGenerationTag {};       struct AcceleratorGenerationTag {}; struct PathGenerationTag {}; struct LinkGenerationTag {};
struct MeasurementGenerationTag {}; struct ReservationGenerationTag {};
struct PolicyGenerationTag {};      struct ObservationGenerationTag {};
struct SourceGenerationTag {};
using HostGeneration = Generation<HostGenerationTag>;
using PciNodeGeneration = Generation<PciNodeGenerationTag>;
using DeviceGeneration = Generation<DeviceGenerationTag>;
using AcceleratorGeneration = Generation<AcceleratorGenerationTag>;
using PathGeneration = Generation<PathGenerationTag>;
using LinkGeneration = Generation<LinkGenerationTag>;
using MeasurementGeneration = Generation<MeasurementGenerationTag>;
using ReservationGeneration = Generation<ReservationGenerationTag>;
using PolicyGeneration = Generation<PolicyGenerationTag>;
using ObservationGeneration = Generation<ObservationGenerationTag>;
using SourceGeneration = Generation<SourceGenerationTag>;

}  // namespace pci

namespace std {
template <typename Tag, typename U>
struct hash<::pci::StrongId<Tag, U>> {
  std::size_t operator()(const ::pci::StrongId<Tag, U>& id) const noexcept {
    return std::hash<U>{}(id.value());
  }
};
template <typename Tag>
struct hash<::pci::Generation<Tag>> {
  std::size_t operator()(const ::pci::Generation<Tag>& g) const noexcept {
    return std::hash<std::uint64_t>{}(g.value());
  }
};
}  // namespace std
