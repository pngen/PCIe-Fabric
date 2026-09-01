# Benchmarks (separate in-memory runtime from OS/CUDA operations).
# Each benchmark prints labeled ops/s and bytes/s, and runs bounded (fixed
# iteration count, optional argv override).

set(PCIEFABRIC_BENCHMARKS
  bench01_bdf
  bench02_path
  bench03_path_rank
  bench04_envelope
  bench05_reservation
  bench06_persistence
  bench07_snapshot
  bench08_planning
)

foreach(bm ${PCIEFABRIC_BENCHMARKS})
  add_executable(${bm} ${bm}.cpp)
  target_link_libraries(${bm} PRIVATE PCIeFabric::pciefabric)
endforeach()
