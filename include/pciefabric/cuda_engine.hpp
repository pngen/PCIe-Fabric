#pragma once
// CUDA measurement engine / RTX 5090 proof. Built only when CUDA is available;
// the core model and discovery never depend on CUDA. All real transfers are
// completed under the benchmark's synchronization contract; nothing is
// fabricated, and negotiated link state is only claimed when actually observed.
#include <cstdint>
#include <string>

#include "pciefabric/bdf.hpp"
#include "pciefabric/enums.hpp"
#include "pciefabric/result.hpp"

namespace pci {

// Real CUDA device identity (PCI BDF from the CUDA runtime).
struct CudaDeviceInfo {
  int ordinal{-1};
  Bdf bdf{};
  std::string name;
  int cc_major{0};
  int cc_minor{0};
  std::uint64_t total_mem{0};
  std::uint64_t free_mem{0};
  bool present{false};
};

struct CudaTransferSample {
  TransferClass cls{TransferClass::UNKNOWN};
  std::uint64_t bytes{0};
  std::uint64_t iters{0};
  double duration_ms{0.0};
  double bytes_per_sec{0.0};   // completed-transfer throughput
  bool ok{false};
  std::string detail;
};

struct CudaProofResult {
  bool ok{false};
  std::string error;
  CudaDeviceInfo device;
  bool pci_correlated{false};
  CudaTransferSample pageable_h2d;
  CudaTransferSample pageable_d2h;
  CudaTransferSample pinned_h2d;
  CudaTransferSample pinned_d2h;
  bool kernel_verified{false};
  std::string kernel_reference;
  std::uint64_t mem_free_before{0};
  std::uint64_t mem_free_after{0};
  bool mem_clean{false};
  bool cuda_clean{false};
};

namespace cudap {
// Detect the first CUDA device and its PCI identity.
[[nodiscard]] Result<CudaDeviceInfo> detect_device();
// Zero-value, then verify a kernel against a CPU reference; returns the
// measured transfer samples.
[[nodiscard]] Result<CudaProofResult> run_proof(std::uint64_t bytes, std::uint64_t iters);
}  // namespace cudap

}  // namespace pci
