cmake_minimum_required(VERSION 3.24)

project(PCIeFabric
  VERSION 1.0.0
  DESCRIPTION "PCIe Fabric - the runtime boundary for PCI Express path identity, capability, locality, measurement, and governed use"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
if(NOT BUILD_TESTING)
  set(BUILD_TESTING OFF)
endif()
include(GNUInstallDirs)

option(PCIEFABRIC_BUILD_CLI "Build the pf command-line front end" ON)
option(PCIEFABRIC_BUILD_EXAMPLES "Build runnable examples" ON)
option(PCIEFABRIC_BUILD_BENCHMARKS "Build benchmarks" ON)
option(PCIEFABRIC_BUILD_CUDA "Build the CUDA measurement engine and proof (auto-detect)" ON)

# Warning policy: project-controlled sources build warning-clean under /W4 /WX (MSVC).
if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/WX> $<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
else()
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Werror;-Wpedantic>)
endif()

find_package(CUDAToolkit QUIET)
set(PCIEFABRIC_HAVE_CUDA OFF)
if(PCIEFABRIC_BUILD_CUDA AND CUDAToolkit_FOUND)
  set(PCIEFABRIC_HAVE_CUDA ON)
  enable_language(CUDA)
  set(CMAKE_CUDA_ARCHITECTURES 120)
  if(NOT CMAKE_CUDA_HOST_COMPILER)
    set(CMAKE_CUDA_HOST_COMPILER "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe")
  endif()
  message(STATUS "PCIeFabric: CUDA toolkit ${CUDAToolkit_VERSION} enabled (measurement + proof, arch ${CMAKE_CUDA_ARCHITECTURES}).")
else()
  message(STATUS "PCIeFabric: CUDA toolkit not found - CUDA engine disabled.")
endif()

add_library(pciefabric STATIC
  src/pciefabric/windows_enum.cpp
  src/pciefabric/tcp.cpp
  src/pciefabric/coordinator.cpp
  src/pciefabric/worker.cpp
)
add_library(PCIeFabric::pciefabric ALIAS pciefabric)

target_include_directories(pciefabric PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

if(WIN32)
  target_compile_definitions(pciefabric PRIVATE PCIEFABRIC_WINDOWS)
  target_link_libraries(pciefabric PUBLIC setupapi cfgmgr32)
endif()

if(PCIEFABRIC_HAVE_CUDA)
  target_sources(pciefabric PRIVATE src/pciefabric/cuda_engine.cu)
  target_compile_definitions(pciefabric PRIVATE PCIEFABRIC_HAVE_CUDA=1)
  target_link_libraries(pciefabric PRIVATE CUDA::cudart)
  set_source_files_properties(src/pciefabric/cuda_engine.cu PROPERTIES COMPILE_OPTIONS "-Xcompiler=/W4;-Xcompiler=/wd4211")
endif()


set_target_properties(pciefabric PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})

if(PCIEFABRIC_BUILD_CLI)
  add_executable(pf cli/main.cpp)
  set_target_properties(pf PROPERTIES OUTPUT_NAME "pf")
  if(PCIEFABRIC_HAVE_CUDA)
    target_compile_definitions(pf PRIVATE PCIEFABRIC_HAVE_CUDA=1)
  endif()
  target_link_libraries(pf PRIVATE PCIeFabric::pciefabric)
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

if(PCIEFABRIC_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(PCIEFABRIC_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

include(CMakePackageConfigHelpers)
install(TARGETS pciefabric EXPORT PCIeFabricTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
if(PCIEFABRIC_BUILD_CLI)
  install(TARGETS pf RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(EXPORT PCIeFabricTargets
  NAMESPACE PCIeFabric::
  FILE PCIeFabricTargets.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PCIeFabric)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PCIeFabricConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/PCIeFabricConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PCIeFabric)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/PCIeFabricConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
  ARCH_INDEPENDENT)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/PCIeFabricConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/PCIeFabricConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PCIeFabric)