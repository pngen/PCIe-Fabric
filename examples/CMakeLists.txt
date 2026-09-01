# Runnable examples (compile as part of normal validation).
# Each example is a small self-contained program with its own main.

set(PCIEFABRIC_EXAMPLES
  example01_discover
  example02_bdf
  example03_accelerator
  example04_bridge_ancestry
  example05_path
  example06_compare_paths
  example07_envelope
  example08_measurement
  example09_reserve_release
  example10_selection
  example11_shared_switch
  example12_cross_root
  example13_stale_path
  example14_recovery
  example15_snapshot
)

foreach(ex ${PCIEFABRIC_EXAMPLES})
  add_executable(${ex} ${ex}.cpp)
  target_link_libraries(${ex} PRIVATE PCIeFabric::pciefabric)
endforeach()

# The accelerator example correlates a real CUDA device when the library was
# built with CUDA. Otherwise the #ifdef branch prints "CUDA unavailable".
if(PCIEFABRIC_HAVE_CUDA)
  target_compile_definitions(example03_accelerator PRIVATE PCIEFABRIC_HAVE_CUDA=1)
  target_link_libraries(example03_accelerator PRIVATE CUDA::cudart)
endif()
