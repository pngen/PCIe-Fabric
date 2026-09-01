// SPDX-License-Identifier: Apache-2.0
// Real Windows PCIe discovery via SetupAPI + Configuration Manager. No GUI
// scraping, no OCR, no command-line-utility presentation parsing.
#define WIN32_LEAN_AND_MEAN
#define _WIN32_DCOM
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devguid.h>

#include <cstdlib>
#include <cstring>
#include <functional>
#include <cwctype>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "pciefabric/windows_enum.hpp"

// Link these provider libraries (also declared in CMake).
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

namespace pci {

namespace {

std::wstring to_w(const std::string& s) { return std::wstring(s.begin(), s.end()); }

std::string wide_to_utf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<std::size_t>(n), 0);
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
  return out;
}

std::string get_registry_string(HDEVINFO devs, SP_DEVINFO_DATA* did, DWORD prop) {
  wchar_t buf[1024];
  DWORD type = 0;
  if (SetupDiGetDeviceRegistryPropertyW(devs, did, prop, &type,
        reinterpret_cast<BYTE*>(buf), static_cast<DWORD>(sizeof(buf)), nullptr)) {
    return wide_to_utf8(buf);
  }
  return {};
}

std::string get_enumerator(HDEVINFO devs, SP_DEVINFO_DATA* did) {
  // Enumerator name via SPDRP_ENUMERATOR_NAME.
  return get_registry_string(devs, did, SPDRP_ENUMERATOR_NAME);
}

// Parse a location-info string like "PCI bus 2, device 0, function 0".
bool parse_location_info(const std::string& s, std::uint8_t& bus, std::uint8_t& dev, std::uint8_t& fn) {
  auto lower = s;
  for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  // find "bus N"
  std::size_t p = lower.find("bus ");
  if (p == std::string::npos) return false;
  bus = static_cast<std::uint8_t>(std::atoi(lower.c_str() + p + 4));
  p = lower.find("device ");
  if (p == std::string::npos) return false;
  dev = static_cast<std::uint8_t>(std::atoi(lower.c_str() + p + 7));
  p = lower.find("function ");
  if (p == std::string::npos) return false;
  fn = static_cast<std::uint8_t>(std::atoi(lower.c_str() + p + 9));
  return true;
}

std::optional<std::uint32_t> get_dword_prop(HDEVINFO devs, SP_DEVINFO_DATA* did, DWORD prop) {
  DWORD type = 0; DWORD val = 0;
  if (SetupDiGetDeviceRegistryPropertyW(devs, did, prop, &type,
        reinterpret_cast<BYTE*>(&val), sizeof(val), nullptr)) return val;
  return std::nullopt;
}

// Parse a PCI hardware id like "PCI\VEN_10DE&DEV_2701&SUBSYS_...&REV_A1".
void parse_hardware_ids(const std::string& hid, RawPciDevice& out) {
  std::size_t p = hid.find("VEN_");
  if (p != std::string::npos) out.vendor_id = hid.substr(p + 4, 4);
  p = hid.find("DEV_");
  if (p != std::string::npos) out.device_id = hid.substr(p + 4, 4);
  p = hid.find("SUBSYS_");
  if (p != std::string::npos) out.subsystem = hid.substr(p + 7, 8);
  p = hid.find("REV_");
  if (p != std::string::npos) out.revision = hid.substr(p + 4);
  p = hid.find("CC_");
  if (p != std::string::npos) out.class_code = hid.substr(p + 3, 6);
}

}  // namespace

DeviceClass classify_pci_class(std::string_view class_code) {
  if (class_code.size() < 2) return DeviceClass::UNKNOWN;
  auto b = std::stoi(std::string(class_code.substr(0, 2)), nullptr, 16);
  auto sub = class_code.size() >= 4 ? std::stoi(std::string(class_code.substr(2, 2)), nullptr, 16) : 0;
  if (b == 0x06 && (sub == 0x04 || sub == 0x09)) return DeviceClass::BRIDGE;   // PCI-PCI / PCIe bridge
  if (b == 0x0C && sub == 0x08) return DeviceClass::ACCELERATOR;                // processing accelerator
  if (b == 0x02) return DeviceClass::NIC;
  if (b == 0x01) return DeviceClass::STORAGE;
  if (b == 0x03) return DeviceClass::OTHER;                                     // display adapter
  if (b == 0x06) return DeviceClass::BRIDGE;
  return DeviceClass::OTHER;
}

DeviceClass classify_by_class_name(std::string_view class_name, std::string_view vendor) {
  if (class_name == "Net") return DeviceClass::NIC;
  if (class_name == "SCSIADAPTER" || class_name == "DiskDrive" || class_name == "HDC" ||
      class_name == "Volume") return DeviceClass::STORAGE;
  if (class_name == "Accelerator") return DeviceClass::ACCELERATOR;
  if (class_name == "Display" && vendor == "10DE") return DeviceClass::ACCELERATOR;  // NVIDIA GPU
  if (class_name == "Display") return DeviceClass::OTHER;
  if (class_name == "PCI") return DeviceClass::BRIDGE;
  if (class_name == "System") return DeviceClass::BRIDGE;
  if (class_name == "MEDIA") return DeviceClass::OTHER;
  return DeviceClass::UNKNOWN;
}

Result<std::vector<RawPciDevice>> enumerate_pci_devices() {
  std::vector<RawPciDevice> out;
  HDEVINFO devs = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
  if (devs == INVALID_HANDLE_VALUE)
    return Result<std::vector<RawPciDevice>>::err(ErrorCode::WINDOWS_ERROR, "SetupDiGetClassDevs failed");

  std::map<DEVINST, std::pair<Bdf, std::string>> inst_info;
  std::set<DEVINST> pci_insts;

  SP_DEVINFO_DATA did{}; did.cbSize = sizeof(did);
  for (DWORD i = 0; SetupDiEnumDeviceInfo(devs, i, &did); ++i) {
    std::string enumerator = get_enumerator(devs, &did);
    if (enumerator != "PCI") continue;
    pci_insts.insert(did.DevInst);
  }

  // First pass: collect PCI devices and their bdf.
  SP_DEVINFO_DATA did2{}; did2.cbSize = sizeof(did2);
  for (DWORD i = 0; SetupDiEnumDeviceInfo(devs, i, &did2); ++i) {
    std::string enumerator = get_enumerator(devs, &did2);
    if (enumerator != "PCI") continue;
    RawPciDevice r;
    r.inst = did2.DevInst;
    std::string loc = get_registry_string(devs, &did2, SPDRP_LOCATION_INFORMATION);
    std::uint8_t bus=0, dev=0, fn=0;
    std::optional<std::uint32_t> busnum = get_dword_prop(devs, &did2, SPDRP_BUSNUMBER);
    if (parse_location_info(loc, bus, dev, fn)) {
      r.bdf = Bdf(0, bus, dev, fn);
    } else if (busnum) {
      std::optional<std::uint32_t> addr = get_dword_prop(devs, &did2, SPDRP_ADDRESS);
      std::uint8_t d = addr ? static_cast<std::uint8_t>((*addr >> 8) & 0x1f) : std::uint8_t(0);
      std::uint8_t f = addr ? static_cast<std::uint8_t>((*addr) & 0x7) : std::uint8_t(0);
      r.bdf = Bdf(0, static_cast<std::uint8_t>(*busnum & 0xff), d, f);
    } else {
      r.bdf = Bdf(0, 0, 0, 0);
    }
    r.enumerator = enumerator;
    auto hid = get_registry_string(devs, &did2, SPDRP_HARDWAREID);
    parse_hardware_ids(hid, r);
    r.driver = get_registry_string(devs, &did2, SPDRP_DRIVER);
    r.device_desc = get_registry_string(devs, &did2, SPDRP_DEVICEDESC);
    r.class_name = get_registry_string(devs, &did2, SPDRP_CLASS);
    // class code from hardware id (may be absent); fall back to class name.
    r.kind = r.class_code.size() >= 2 ? classify_pci_class(r.class_code) : classify_by_class_name(r.class_name, r.vendor_id);
    r.is_bridge = (r.kind == DeviceClass::BRIDGE);
    inst_info[did2.DevInst] = {r.bdf, enumerator};
    out.push_back(r);
  }
  SetupDiDestroyDeviceInfoList(devs);

  // Second pass: resolve parent ancestry via CM_Get_Parent.
  for (auto& r : out) {
    DEVINST parent = 0;
    CONFIGRET cr = CM_Get_Parent(&parent, static_cast<DEVINST>(r.inst), 0);
    if (cr == CR_SUCCESS && parent != 0) {
      auto it = inst_info.find(parent);
      if (it != inst_info.end()) r.parent_bdf = it->second.first;
    }
  }

  // Deterministic order by canonical BDF.
  std::sort(out.begin(), out.end(), [](const RawPciDevice& a, const RawPciDevice& b) { return a.bdf < b.bdf; });
  return Result<std::vector<RawPciDevice>>::success(std::move(out));
}

Result<Topology> build_topology_from_windows() {
  auto devices = enumerate_pci_devices();
  if (!devices) return Result<Topology>::err(devices.code(), devices.message());
  Topology topo;
  // Map from bdf -> node id
  std::map<Bdf, PciNodeId> by_bdf;
  std::uint64_t next = 1;
  std::set<Bdf> bridged;
  for (const auto& d : devices.value()) {
    if (d.is_bridge) bridged.insert(d.bdf);
  }
  for (const auto& d : devices.value()) {
    DeviceClass kind = d.kind;
    if (d.is_bridge) kind = DeviceClass::BRIDGE;
    if (!d.bdf.valid()) continue;
    PciNodeId id(next++);
    // Determine parent node if present.
    PciNodeId parent(0);
    if (d.parent_bdf) {
      auto it = by_bdf.find(*d.parent_bdf);
      if (it != by_bdf.end()) parent = it->second;
    }
    auto r = topo.add_endpoint(id, d.bdf, kind, PciNodeGeneration(1), Provenance::REPORTED, parent);
    if (r) by_bdf[d.bdf] = id;
  }
  return Result<Topology>::success(std::move(topo));
}

}  // namespace pci