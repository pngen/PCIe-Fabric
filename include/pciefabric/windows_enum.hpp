#pragma once
// Real Windows PCIe discovery backend (SetupAPI / Configuration Manager).
// Enumerates the actual PCI device tree and its ancestor relationships. Missing
// hierarchy facts remain explicitly UNKNOWN; nothing is fabricated from
// internet specifications.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pciefabric/bdf.hpp"
#include "pciefabric/enums.hpp"
#include "pciefabric/result.hpp"
#include "pciefabric/topology.hpp"

namespace pci {

// A raw device discovered from SetupAPI. Fields that are unavailable are left
// as std::optional (UNKNOWN).
struct RawPciDevice {
  Bdf bdf{};
  std::string enumerator;
  std::string vendor_id;
  std::string device_id;
  std::string subvendor;
  std::string subsystem;
  std::string revision;
  std::string class_code;   // e.g. "030000" (base, subclass, progif)
  std::string driver;
  std::string device_desc;
  std::string class_name;
  DeviceClass kind{DeviceClass::UNKNOWN};
  std::uint64_t inst{0};    // CM_DEVINST for parent resolution
  std::optional<Bdf> parent_bdf;  // resolved from parent chain
  bool is_bridge{false};
  bool is_root{false};
};

// Enumerate all present PCI devices via SetupAPI. Deterministic order.
[[nodiscard]] Result<std::vector<RawPciDevice>> enumerate_pci_devices();

// Classify a device based on its PCI class code (if known).
[[nodiscard]] DeviceClass classify_pci_class(std::string_view class_code);
// Classify based on the Windows device class name and vendor id (fallback).
[[nodiscard]] DeviceClass classify_by_class_name(std::string_view class_name, std::string_view vendor);

// Build a Topology from the enumerated devices (bridges, switches, endpoints,
// and parent-child relationships). Deterministic.
[[nodiscard]] Result<Topology> build_topology_from_windows();

}  // namespace pci