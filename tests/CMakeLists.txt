# PCIe Fabric tests: unit/property/adversarial/concurrency/synthetic +
# multiprocess authority proof. No test timeout mechanism is used.

add_executable(test_unit test_unit.cpp)
target_link_libraries(test_unit PRIVATE PCIeFabric::pciefabric)
if(PCIEFABRIC_HAVE_CUDA)
  target_link_libraries(test_unit PRIVATE CUDA::cudart)
endif()
add_test(NAME unit_property_command COMMAND test_unit)

add_executable(test_multiprocess test_multiprocess.cpp)
target_link_libraries(test_multiprocess PRIVATE PCIeFabric::pciefabric)
if(PCIEFABRIC_HAVE_CUDA)
  target_link_libraries(test_multiprocess PRIVATE CUDA::cudart)
endif()
add_dependencies(test_multiprocess pf)
add_test(NAME multiprocess_authority COMMAND test_multiprocess)
