# PCIe Fabric

PCIe Fabric is the runtime boundary for PCIe path identity, capability, locality,
measurement, and governed use across heterogeneous AI infrastructure.

It answers one systems question:

> What PCIe path connects this accelerator, CPU, NIC, storage device, or memory
> endpoint, what capabilities and bottlenecks does that path expose, how is it
> behaving now, and when should infrastructure choose, avoid, revalidate, or
> reroute through it?

PCIe Fabric is not a generic PCI device lister, topology viewer, synthetic
bandwidth calculator, or thin wrapper around operating-system enumeration APIs.
It distinguishes **structural topology facts** from **measured transport
behavior**. It never infers measured throughput from generation and lane width,
never infers physical link width from observed transfer speed, never silently
converts theoretical bandwidth into available bandwidth, and never fabricates
negotiated-link properties that cannot be obtained reliably on the host.

## Architecture

PCIe Fabric is a C++20 library (namespace `pci`) plus a thin `pf` CLI. The
core is constructed from strong typed identities, distinct mutable generations,
explicit provenance, and versioned binary persistence:

| Concern | Header |
| --- | --- |
| Strong identities + generations | `pciefabric/ids.hpp` |
| Typed physical quantities | `pciefabric/values.hpp` |
| Semantic enums (provenance/freshness/locality/...) | `pciefabric/enums.hpp` |
| Canonical BDF identity | `pciefabric/bdf.hpp` |
| PCIe hierarchy / topology / locality | `pciefabric/topology.hpp` |
| Theoretical link envelope | `pciefabric/bandwidth.hpp` |
| Link capability (optional, never fabricated) | `pciefabric/link.hpp` |
| Paths, path classes | `pciefabric/path.hpp` |
| Measurements (completed work only) | `pciefabric/measurement.hpp` |
| Governed reservations + accounting | `pciefabric/reservation.hpp` |
| Deterministic path selection | `pciefabric/selection.hpp` |
| Provenance + freshness | `pciefabric/provenance.hpp` |
| Snapshots / diff / replay / semantic digest | `pciefabric/snapshot.hpp` |
| Versioned binary codec + CRC-32 | `pciefabric/codec.hpp` |
| Persistence (atomic, validated) | `pciefabric/persistence.hpp` |
| Framed-TCP wire protocol | `pciefabric/protocol.hpp` |
| Coordinator + worker | `pciefabric/coordinator.hpp` `worker.hpp` |
| Runtime facade | `pciefabric/runtime.hpp` |
| Windows discovery backend | `pciefabric/windows_enum.hpp` |
| CUDA measurement engine / proof (optional) | `pciefabric/cuda_engine.hpp` |

### Relationship to Topology Fabric and NUMA Fabric

* **Topology Fabric** answers: what hardware topology exists, what connects it,
  and what that implies about locality and path cost.
* **NUMA Fabric** answers: where should CPU execution and host memory live
  relative to accelerators, NICs, storage, and other memory domains.

**PCIe Fabric owns the PCIe-specific runtime boundary.** It consumes topology
and locality facts and produces the PCIe path identity, capability, contention,
reservation, freshness, and authority state that the other fabrics build upon.
It does not duplicate their role.

## Endpoint identity

Every device has a stable canonical **BDF** (`bus:device.function`,
optionally `domain:bus:device.function`) and a distinct typed identity. A BDF
is **not** by itself sufficient authority for mutable runtime state. A device
disappearing and reappearing at the same BDF receives a fresh generation; a
restarted worker receives a fresh `WorkerBootId`; a stale observation from a
prior incarnation can never become current.

## PCIe hierarchy

The `Topology` model represents root complexes, bridges, switches, and
endpoints with explicit upstream/downstream relationships, bridge chains,
sibling endpoints, common upstream ancestors, and root-complex association.
Missing hierarchy facts are represented explicitly as `UNKNOWN`; nothing is
invented. The model rejects hierarchy cycles, duplicate current identities, and
invalid relationships. The Windows backend discovers the real tree through
SetupAPI and the Configuration Manager — never by scraping GUI output, OCR, or
command-line presentation.

## Path construction

A `Path` is built from the topology and carries the source/destination
endpoints, its semantic `PathClass`, the bridge chain, hop count, locality,
capabilities, theoretical envelope, measured evidence, contention, freshness,
confidence, provenance, and authority generations. Peer accessibility,
GPUDirect, peer memory, and transport-offload capabilities are kept as separate
explicit capabilities — sharing a hierarchy never implies peer capability.

## Theoretical vs. measured bandwidth

The `LinkEnvelope` computes the **DERIVED** theoretical link envelope using the
correct per-generation signaling rate (GT/s) and encoding overhead (8b/10b for
Gen1/2, 128b/130b for Gen3+), producing a raw aggregate and a protocol-effective
payload envelope. Decimal GT/s and binary GiB/s are kept strictly separate. It is
never presented as measured throughput. Negotiated link state is only reported
where a backend truly exposes it; otherwise it is `UNKNOWN`.

## Provenance and freshness

Every relevant field carries an explicit provenance: `MEASURED`, `REPORTED`,
`DERIVED`, `ESTIMATED`, `SYNTHETIC`, or `UNKNOWN`. Freshness transitions
among `CURRENT`, `AGING`, `STALE`, `REVALIDATION_REQUIRED`, and
`INVALID`. Recovered persisted physical observations are never automatically
`CURRENT` after a restart; they enter `REVALIDATION_REQUIRED`.

## Path selection

The deterministic `PathSelector` ranks candidate paths component-wise (never a
single opaque score) and returns a typed `SelectionDecision`: `USE_PATH`,
`USE_PATH_WITH_PENALTY`, `RESERVE_PATH`, `DEFER`, `REJECT`, or
`REVALIDATION_REQUIRED`. Each decision exposes the selected path, alternatives,
eliminated paths, hard constraints, the binding constraint, locality, envelope,
measured evidence, freshness, confidence, contention, policy/authority
generations, and what would change the result.

## Reservations

`ReservationRegistry` provides atomic, fenced reservations with per-path
capacity that is a software governance ceiling (not measured hardware
bandwidth). It rejects double reservation, overcommit beyond configured hard
policy, stale release, duplicate release, generation rollback, and accounting
drift. Concurrent reserve/release stress closes to exactly zero.

## Contention and shared paths

Two endpoints that share an upstream bridge/root/switch may compete even when
their endpoint-local links differ. `Topology` exposes the structural shared
contention domain. Contention observations (`IDLE`, `LOW`, `MODERATE`,
`HIGH`, `SATURATED`) are kept separate from structural sharing, and only
measured states are used as evidence; synthetic scenarios may force controlled
states for decision testing.

## Distributed authority

The framed-TCP protocol (`magic | version | type | length | CRC-32 | payload`)
rejects malformed magic, unsupported version, impossible lengths, truncation,
checksum corruption, invalid enums/counts, duplicate ids, malformed BDFs, stale
epochs, stale boots, and stale generations. The coordinator accepts current path
authority and fences all mutable operations by `CoordinatorEpoch`,
`WorkerBootId`, and the relevant generations. A restarted worker with a fresh
boot can re-register; old measurements and reservations cannot mutate current
state. Socket no-data conditions are not mistaken for permanent connection
failure; thread exceptions never crash the coordinator.

## Persistence and recovery

State is written atomically (temp → flush → close → rename) with a file magic,
version, bounded counts, and CRC-32. Corruption, truncation, duplicate ids,
impossible counts, invalid enums/generations/BDFs, and trailing garbage are
rejected on load. Recovered physical observations require revalidation before
they may be treated as current.

## Windows discovery backend

The SetupAPI / Configuration Manager backend enumerates the real PCI tree,
resolves parent ancestry, and classifies devices. On the development host it
discovered **63 PCI devices** including the **NVIDIA GeForce RTX 5090** at
`0000:01:00.0` (vendor `10DE`, device `2B85`), whose parent upstream
bridge is `0000:00:01.1`. Negotiated link generation/width were **not
confirmed programmatically** on this host and are reported as `UNKNOWN` rather
than filled from internet specifications.

## CUDA validation

The optional CUDA engine (CUDA 13.1, `sm_120`) discovers the real CUDA device,
correlates its PCI identity with the OS inventory, allocates pageable host,
pinned host, and device memory, and runs real completed transfers:

| Transfer class | Measured (bounded 256 MiB, 8 iterations) |
| --- | --- |
| Pageable H2D | 18.65 GiB/s |
| Pageable D2H | 17.81 GiB/s |
| Pinned H2D | 26.47 GiB/s |
| Pinned D2H | 18.37 GiB/s |

A real CUDA kernel was launched, synchronized, copied back, and verified against
a CPU reference; all allocations were released and CUDA/memory accounting
returned to the baseline. The measured throughput is far below the theoretical
Gen5 x16 envelope (≈58.7 GiB/s) because these are host memcpy paths, not PCIe
saturation — PCIe saturation is not claimed. Peer/GPUDirect/RDMA execution was
not exercised and is not claimed.

## Real vs. synthetic proof

The real host proof uses the real Windows inventory and the real RTX 5090. The
host exposes limited PCIe path diversity (a single GPU), so the
multi-endpoint scenarios — dual-GPU behind a switch, cross-root placement,
GPU+NIC sharing an upstream bottleneck, negotiated degradation, required
capability unavailable, contention-saturated deferral, stale-worker rejection,
path-generation rollover, and deterministic ranking — are exercised through
deterministic **synthetic** models built on the **same production** graph and
path-selection machinery, and are labeled `SYNTHETIC`. No physical multi-GPU
proof is claimed.

## CLI

`pf` is a thin front end over the library. Supported commands include
`discover`, `devices`, `bandwidth`, `topology`/`paths`,
`cuda-proof`, `save`/`load`, `validate-bdf`, `coordinator`, and
`worker`. The CLI never duplicates core logic.

## Examples

Fifteen runnable examples live under `examples/` (discovery, BDF identity,
accelerator resolution, bridge ancestry, path resolution, path comparison,
theoretical envelope, measurement ingest, reserve/release, selection
explanation, shared-switch simulation, cross-root placement, stale-path
rejection, persistence/recovery, and snapshot/diff).

## Benchmarks

Benchmarks under `benchmarks/` measure separate workloads (operations/s,
bytes/s, latency, serialization throughput, wall time) and keep in-memory
runtime separate from OS/CUDA operations.

## Build and install

Requires C++20, CMake ≥ 3.24, Ninja, and MSVC 19.4x. CUDA is optional (and, when
enabled, uses CUDA 13.1 with `sm_120`).

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
cmake --install build --prefix <prefix>
```

Project-controlled C++ sources build warning-clean under `/W4 /WX`; the CUDA
source is warning-clean, with only vendor CUDA host-stub warnings suppressed
narrowly at that compilation boundary.

## Downstream CMake use

```
find_package(PCIeFabric CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE PCIeFabric::pciefabric)
```

An independent consumer built outside the source tree using
`find_package(PCIeFabric CONFIG REQUIRED)` compiled, linked, and ran against
the exported target successfully.

## Limitations

* Negotiated PCIe generation/lane width is only reported when a backend truly
  exposes it; on this host it is `UNKNOWN`.
* Measured throughput is host memcpy throughput, not PCIe bus saturation.
* Peer access / GPUDirect / RDMA were not physically exercised and are not
  claimed.
* Physical multi-GPU and multi-root proofs are represented synthetically; only
  single-GPU hardware evidence is claimed.
* Device classification uses Windows device-class names and PCI class codes;
  where neither is exposed, a device is reported as `UNKNOWN`.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
