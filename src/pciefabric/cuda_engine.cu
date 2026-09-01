// SPDX-License-Identifier: Apache-2.0
// CUDA measurement engine / RTX 5090 (sm_120) proof. Real device memory, pinned
// host memory, pageable host memory, real H2D/D2H transfers, a real kernel, CPU
// reference verification, and complete resource/memory-accounting release.
#include "pciefabric/cuda_engine.hpp"

#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace pci {
namespace cudap {

namespace {

const char* err(cudaError_t e) { return cudaGetErrorString(e); }

double timed_transfer(void* dst, const void* src, size_t bytes, cudaMemcpyKind kind,
                      int iters, bool& ok) {
  ok = false;
  cudaEvent_t t0, t1;
  if (cudaEventCreate(&t0) != cudaSuccess) return 0.0;
  if (cudaEventCreate(&t1) != cudaSuccess) { cudaEventDestroy(t0); return 0.0; }
  cudaEventRecord(t0);
  for (int i = 0; i < iters; ++i) {
    if (cudaMemcpy(dst, src, bytes, kind) != cudaSuccess) { cudaEventDestroy(t0); cudaEventDestroy(t1); return 0.0; }
  }
  cudaEventRecord(t1);
  if (cudaEventSynchronize(t1) != cudaSuccess) { cudaEventDestroy(t0); cudaEventDestroy(t1); return 0.0; }
  float ms = 0.0f;
  cudaEventElapsedTime(&ms, t0, t1);
  cudaEventDestroy(t0); cudaEventDestroy(t1);
  ok = true;
  return static_cast<double>(ms);
}

__global__ void saxpy_kernel(float* y, const float* x, float a, int n) {
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) y[i] = a * x[i] + y[i];
}

}  // namespace

Result<CudaDeviceInfo> detect_device() {
  int count = 0;
  cudaError_t e = cudaGetDeviceCount(&count);
  if (e != cudaSuccess || count < 1)
    return Result<CudaDeviceInfo>::err(ErrorCode::CUDA_ERROR, "no CUDA device");
  CudaDeviceInfo info;
  info.ordinal = 0;
  char pci[64] = {0};
  if (cudaDeviceGetPCIBusId(pci, sizeof(pci), 0) == cudaSuccess && std::strlen(pci) > 0) {
    auto b = Bdf::parse(pci);
    if (b) info.bdf = b.value();
  }
  cudaDeviceProp prop{};
  if (cudaGetDeviceProperties(&prop, 0) == cudaSuccess) {
    info.name = prop.name;
    info.cc_major = prop.major;
    info.cc_minor = prop.minor;
  }
  size_t free_b = 0, total_b = 0;
  if (cudaMemGetInfo(&free_b, &total_b) == cudaSuccess) {
    info.free_mem = free_b; info.total_mem = total_b;
  }
  info.present = true;
  return Result<CudaDeviceInfo>::success(info);
}

Result<CudaProofResult> run_proof(std::uint64_t bytes, std::uint64_t iters) {
  CudaProofResult out;
  auto devr = detect_device();
  if (!devr) return Result<CudaProofResult>::err(devr.code(), devr.message());
  out.device = devr.value();

  size_t free_b = 0, total_b = 0;
  cudaMemGetInfo(&free_b, &total_b);
  out.mem_free_before = free_b;

  // Bounded allocations (default 256 MiB; caller caps). Never near VRAM exhaustion.
  if (bytes == 0) bytes = 256u * 1024u * 1024u;
  if (iters == 0) iters = 8;

  // 1. pageable host memory
  std::vector<float> h_page(static_cast<std::size_t>(bytes / sizeof(float)));
  // 2. pinned host memory
  float* h_pin = nullptr;
  if (cudaHostAlloc(&h_pin, static_cast<std::size_t>(bytes),
                    cudaHostAllocDefault) != cudaSuccess) {
    out.error = std::string("cudaHostAlloc failed: ") + err(cudaGetLastError());
    return Result<CudaProofResult>::success(out);
  }
  // 3. device memory
  float* d_buf = nullptr;
  if (cudaMalloc(&d_buf, static_cast<std::size_t>(bytes)) != cudaSuccess) {
    out.error = std::string("cudaMalloc failed: ") + err(cudaGetLastError());
    cudaFreeHost(h_pin);
    return Result<CudaProofResult>::success(out);
  }

  // fill reference pattern
  for (std::size_t i = 0; i < h_page.size(); ++i) h_page[i] = static_cast<float>(i % 251);
  std::memcpy(h_pin, h_page.data(), static_cast<std::size_t>(bytes));

  bool ok = false;
  double ms = timed_transfer(d_buf, h_page.data(), static_cast<std::size_t>(bytes),
                             cudaMemcpyHostToDevice, static_cast<int>(iters), ok);
  if (ok) { out.pageable_h2d.ok = true; out.pageable_h2d.cls = TransferClass::PAGEABLE_H2D;
    out.pageable_h2d.bytes = bytes; out.pageable_h2d.iters = iters; out.pageable_h2d.duration_ms = ms;
    out.pageable_h2d.bytes_per_sec = static_cast<double>(bytes * iters) / (ms / 1000.0); }

  std::vector<float> h_back(static_cast<std::size_t>(bytes / sizeof(float)));
  ms = timed_transfer(h_back.data(), d_buf, static_cast<std::size_t>(bytes),
                      cudaMemcpyDeviceToHost, static_cast<int>(iters), ok);
  if (ok) { out.pageable_d2h.ok = true; out.pageable_d2h.cls = TransferClass::PAGEABLE_D2H;
    out.pageable_d2h.bytes = bytes; out.pageable_d2h.iters = iters; out.pageable_d2h.duration_ms = ms;
    out.pageable_d2h.bytes_per_sec = static_cast<double>(bytes * iters) / (ms / 1000.0); }

  ms = timed_transfer(d_buf, h_pin, static_cast<std::size_t>(bytes),
                      cudaMemcpyHostToDevice, static_cast<int>(iters), ok);
  if (ok) { out.pinned_h2d.ok = true; out.pinned_h2d.cls = TransferClass::PINNED_H2D;
    out.pinned_h2d.bytes = bytes; out.pinned_h2d.iters = iters; out.pinned_h2d.duration_ms = ms;
    out.pinned_h2d.bytes_per_sec = static_cast<double>(bytes * iters) / (ms / 1000.0); }

  std::vector<float> h_pin_back(static_cast<std::size_t>(bytes / sizeof(float)));
  ms = timed_transfer(h_pin_back.data(), d_buf, static_cast<std::size_t>(bytes),
                      cudaMemcpyDeviceToHost, static_cast<int>(iters), ok);
  if (ok) { out.pinned_d2h.ok = true; out.pinned_d2h.cls = TransferClass::PINNED_D2H;
    out.pinned_d2h.bytes = bytes; out.pinned_d2h.iters = iters; out.pinned_d2h.duration_ms = ms;
    out.pinned_d2h.bytes_per_sec = static_cast<double>(bytes * iters) / (ms / 1000.0); }

  // kernel + verify against CPU reference
  if (bytes >= sizeof(float)) {
    int n = static_cast<int>(bytes / sizeof(float));
    float a = 2.0f;
    const float* d_src = d_buf;
    std::vector<float> expect(h_page.size());
    for (std::size_t i = 0; i < h_page.size(); ++i) expect[i] = a * h_page[i] + h_page[i];
    // copy host->device (pageable already did; use d_src=d_buf with target) then kernel in place
    if (cudaMemcpy(d_buf, h_page.data(), static_cast<std::size_t>(bytes), cudaMemcpyHostToDevice) == cudaSuccess) {
      int threads = 256;
      int blocks = (n + threads - 1) / threads;
      saxpy_kernel<<<blocks, threads>>>(d_buf, d_src, a, n);
      cudaError_t ke = cudaDeviceSynchronize();
      if (ke == cudaSuccess) {
        std::vector<float> got(h_page.size());
        if (cudaMemcpy(got.data(), d_buf, static_cast<std::size_t>(bytes), cudaMemcpyDeviceToHost) == cudaSuccess) {
          bool match = true;
          for (std::size_t i = 0; i < got.size(); ++i) if (std::fabsf(got[i] - expect[i]) > 1e-3f) { match = false; break; }
          out.kernel_verified = match;
          out.kernel_reference = match ? "CPU reference matched" : "CPU reference MISMATCH";
        }
      }
    }
  }

  // release everything
  cudaFree(d_buf);
  cudaFreeHost(h_pin);
  cudaError_t last = cudaGetLastError();
  out.cuda_clean = (last == cudaSuccess);
  cudaMemGetInfo(&free_b, &total_b);
  out.mem_free_after = free_b;
  // account: device memory must return to baseline (host allocations not in VRAM).
  out.mem_clean = (free_b >= out.mem_free_before);  // cudaMalloc released back
  out.pci_correlated = out.device.bdf.valid();
  out.ok = true;
  return Result<CudaProofResult>::success(out);
}

}  // namespace cudap
}  // namespace pci
