cmake_minimum_required(VERSION 3.25)

project(PortFabric
  VERSION 1.0.0
  DESCRIPTION "Authoritative port-governance runtime of the Distributed Fabric Infrastructure stack"
  HOMEPAGE_URL "https://github.com/pngen/Port-Fabric"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(PF_BUILD_TESTS "Build the Port Fabric test suite" ON)
option(PF_BUILD_EXAMPLES "Build the Port Fabric examples" ON)
option(PF_BUILD_TOOLS "Build the Port Fabric command line tools" ON)
option(PF_BUILD_BENCHMARKS "Build the Port Fabric benchmarks" OFF)
option(PF_ENABLE_ASAN "Build with AddressSanitizer (MSVC /fsanitize=address)" OFF)
option(PF_ENABLE_ANALYZE "Build with MSVC /analyze static analysis" OFF)
option(PF_WARNINGS_AS_ERRORS "Treat first-party compiler warnings as errors" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

include(GNUInstallDirs)
include(GenerateExportHeader)
include(CMakePackageConfigHelpers)
include(CTest)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# First-party warning policy: /W4 /WX /permissive- on MSVC, -Wall -Wextra
# -Wpedantic -Wshadow -Werror elsewhere. No warning is suppressed anywhere in
# first-party code; the build is clean at this level.
# ---------------------------------------------------------------------------
function(pf_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE
      /W4
      /permissive-
      /utf-8
      /Zc:__cplusplus
      /Zc:preprocessor
      /Zc:throwingNew
      /EHsc)
    if(PF_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
    if(PF_ENABLE_ANALYZE)
      # /analyze is the strongest static analysis available with this toolchain.
      # Findings in first-party code are reported and never suppressed; the
      # Windows SDK headers are declared external so that their own annotations
      # cannot mask or drown first-party results.
      target_compile_options(${target} PRIVATE
        /analyze
        /analyze:external-
        /external:anglebrackets
        /external:W0)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(PF_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
set(PF_PUBLIC_HEADERS
  include/portfabric/adapter.hpp
  include/portfabric/administrative.hpp
  include/portfabric/authority.hpp
  include/portfabric/capability.hpp
  include/portfabric/characteristics.hpp
  include/portfabric/client.hpp
  include/portfabric/configuration.hpp
  include/portfabric/diff.hpp
  include/portfabric/digest.hpp
  include/portfabric/engine.hpp
  include/portfabric/explanation.hpp
  include/portfabric/generation.hpp
  include/portfabric/ids.hpp
  include/portfabric/lifecycle.hpp
  include/portfabric/limits.hpp
  include/portfabric/outcome.hpp
  include/portfabric/ownership.hpp
  include/portfabric/persistence.hpp
  include/portfabric/platform.hpp
  include/portfabric/profile.hpp
  include/portfabric/protocol.hpp
  include/portfabric/record.hpp
  include/portfabric/server.hpp
  include/portfabric/snapshot.hpp
  include/portfabric/synthetic.hpp
  include/portfabric/version.hpp)

set(PF_SOURCES
  src/adapter.cpp
  src/administrative.cpp
  src/authority.cpp
  src/capability.cpp
  src/characteristics.cpp
  src/client.cpp
  src/codec.cpp
  src/configuration.cpp
  src/diff.cpp
  src/digest.cpp
  src/engine.cpp
  src/engine_explain.cpp
  src/engine_mutation.cpp
  src/engine_persistence.cpp
  src/engine_queries.cpp
  src/engine_support.cpp
  src/explanation.cpp
  src/ids.cpp
  src/lifecycle.cpp
  src/net.cpp
  src/outcome.cpp
  src/ownership.cpp
  src/persistence.cpp
  src/platform_windows.cpp
  src/profile.cpp
  src/protocol.cpp
  src/record.cpp
  src/server.cpp
  src/sha256.cpp
  src/snapshot.cpp
  src/synthetic.cpp
  src/text.cpp
  src/version.cpp
  src/wire.cpp)

add_library(PortFabric ${PF_SOURCES} ${PF_PUBLIC_HEADERS})
add_library(SummonSoftwareLabs::PortFabric ALIAS PortFabric)

generate_export_header(PortFabric
  BASE_NAME PF
  EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/portfabric/export.hpp")

target_include_directories(PortFabric
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_link_libraries(PortFabric PRIVATE Threads::Threads)
pf_apply_warnings(PortFabric)

target_compile_definitions(PortFabric
  PRIVATE PF_BUILDING_LIBRARY
  PUBLIC PF_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
         PF_VERSION_MINOR=${PROJECT_VERSION_MINOR}
         PF_VERSION_PATCH=${PROJECT_VERSION_PATCH})

if(WIN32)
  target_link_libraries(PortFabric PRIVATE ws2_32 iphlpapi)
endif()

if(PF_ENABLE_ASAN)
  if(MSVC)
    target_compile_options(PortFabric PUBLIC /fsanitize=address /Zi)
    target_link_options(PortFabric PUBLIC /INCREMENTAL:NO)
  else()
    target_compile_options(PortFabric PUBLIC -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(PortFabric PUBLIC -fsanitize=address)
  endif()
endif()

set_target_properties(PortFabric PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Tools
# ---------------------------------------------------------------------------
if(PF_BUILD_TOOLS)
  add_executable(pf_inspect apps/pf_inspect/main.cpp)
  target_link_libraries(pf_inspect PRIVATE SummonSoftwareLabs::PortFabric)
  pf_apply_warnings(pf_inspect)
  set_target_properties(pf_inspect PROPERTIES OUTPUT_NAME pf-inspect)

  add_executable(pf_coordinator apps/pf_coordinator/main.cpp)
  target_link_libraries(pf_coordinator PRIVATE SummonSoftwareLabs::PortFabric)
  pf_apply_warnings(pf_coordinator)
  set_target_properties(pf_coordinator PROPERTIES OUTPUT_NAME pf-coordinator)

  add_executable(pf_worker apps/pf_worker/main.cpp)
  target_link_libraries(pf_worker PRIVATE SummonSoftwareLabs::PortFabric)
  pf_apply_warnings(pf_worker)
  set_target_properties(pf_worker PROPERTIES OUTPUT_NAME pf-worker)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(PF_BUILD_EXAMPLES)
  set(PF_EXAMPLES
    ex01_basic_configuration
    ex02_administrative_state
    ex03_profile_assignment
    ex04_stale_generation
    ex05_capability_rejection
    ex06_worker_fencing
    ex07_topology_invalidation
    ex08_device_replacement
    ex09_persistence_recovery
    ex10_synthetic_breakout)
  foreach(example IN LISTS PF_EXAMPLES)
    add_executable(${example} examples/${example}.cpp)
    target_link_libraries(${example} PRIVATE SummonSoftwareLabs::PortFabric)
    pf_apply_warnings(${example})
  endforeach()
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
if(PF_BUILD_BENCHMARKS)
  add_executable(pf_benchmarks benchmarks/bench_engine.cpp)
  target_link_libraries(pf_benchmarks PRIVATE SummonSoftwareLabs::PortFabric)
  pf_apply_warnings(pf_benchmarks)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(PF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install / export
# ---------------------------------------------------------------------------
install(TARGETS PortFabric
  EXPORT PortFabricTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(PF_BUILD_TOOLS)
  install(TARGETS pf_inspect pf_coordinator pf_worker
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(DIRECTORY include/portfabric
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.hpp")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/portfabric/export.hpp"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/portfabric)

install(EXPORT PortFabricTargets
  FILE PortFabricTargets.cmake
  NAMESPACE SummonSoftwareLabs::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PortFabric)

configure_package_config_file(
  cmake/PortFabricConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/PortFabricConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PortFabric)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/PortFabricConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/PortFabricConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/PortFabricConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/PortFabric)
