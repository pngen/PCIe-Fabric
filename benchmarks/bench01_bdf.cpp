// SPDX-License-Identifier: Apache-2.0
// Benchmark 01: BDF parse + canonical format. Reports ops/s and bytes/s.
#include "pciefabric/bdf.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
  const char* inputs[] = {"0000:01:00.0", "0000:03:1f.7", "0a:00.1", "ffff:ff:1f.0"};
  const std::uint64_t default_n = 200000;
  std::uint64_t n = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : default_n;
  if (n == 0) n = 1;
  if (n > 2000000) n = 2000000;

  std::uint64_t input_bytes = 0;
  for (const char* s : inputs) input_bytes += std::strlen(s);

  std::uint64_t ok = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (std::uint64_t i = 0; i < n; ++i) {
    const char* s = inputs[i % 4];
    auto r = pci::Bdf::parse(s);
    if (r) { (void)r.value().canonical(); ++ok; }
  }
  auto t1 = std::chrono::steady_clock::now();
  double sec = std::chrono::duration<double>(t1 - t0).count();
  if (sec <= 0.0) sec = 1e-9;
  double ops = static_cast<double>(n) / sec;
  double bytes_per_sec = (static_cast<double>(input_bytes) * static_cast<double>(n)) / sec;
  std::printf("BDF parse/format   : %llu ops in %.3f s -> %.0f ops/s, %.1f MiB/s (input bytes)\n",
              static_cast<unsigned long long>(ok), sec, ops, bytes_per_sec / (1024.0 * 1024.0));
  return 0;
}
