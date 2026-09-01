// SPDX-License-Identifier: Apache-2.0
// Example 02: Inspect PCI BDF values - parse, format, canonicalize, and reject
// invalid encodings.
#include "pciefabric/bdf.hpp"
#include <cstdio>
#include <string_view>

int main() {
  const char* inputs[] = {"01:00.0", "0000:03:1f.7", "0a:00.1", "ffff:ff:1f.0"};
  for (const char* s : inputs) {
    auto r = pci::Bdf::parse(s);
    if (!r) {
      std::printf("parse '%s' FAILED: %s (code=%d)\n", s, r.message().c_str(), static_cast<int>(r.code()));
      continue;
    }
    const pci::Bdf& b = r.value();
    std::printf("parse '%-14s' -> bdf='%-8s' canonical='%-18s' domain=%u bus=%u dev=%u func=%u valid=%d\n",
                s, b.bdf_string().c_str(), b.canonical().c_str(),
                static_cast<unsigned>(b.domain()), static_cast<unsigned>(b.bus()),
                static_cast<unsigned>(b.device()), static_cast<unsigned>(b.function()),
                static_cast<int>(b.valid()));
    auto id = pci::Bdf::to_identity(s);
    if (id) std::printf("    to_identity=%llu\n", static_cast<unsigned long long>(id.value().value()));
  }

  // Reject invalid encodings.
  const char* rejects[] = {"zz:00.0", "01:00.9", "01:00", "1:0.0", "01:00:00"};
  for (const char* s : rejects) {
    auto r = pci::Bdf::parse(s);
    std::printf("reject '%s' -> ok=%d code=%d msg=%s\n", s,
                static_cast<int>(r.ok()), static_cast<int>(r.code()), r.message().c_str());
  }

  // Construct directly and round-trip formatting.
  pci::Bdf b(0, 1, 2, 0);
  std::printf("direct bdf_string='%s' canonical='%s' has_domain=%d\n",
              b.bdf_string().c_str(), b.canonical().c_str(), static_cast<int>(b.has_domain()));
  return 0;
}
