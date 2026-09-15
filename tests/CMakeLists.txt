# Port Fabric test suite.
#
# Every test is a plain executable registered with CTest. No test uses a timeout:
# a hanging test is a runtime defect, not something to mask by terminating the
# process.

function(pf_add_test name)
  add_executable(${name} test_main.cpp ${ARGN})
  target_link_libraries(${name} PRIVATE SummonSoftwareLabs::PortFabric)
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  pf_apply_warnings(${name})
  add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endfunction()

pf_add_test(pf_test_model test_model.cpp)
pf_add_test(pf_test_engine test_engine.cpp)
pf_add_test(pf_test_capability_profile test_capability_profile.cpp)
pf_add_test(pf_test_persistence test_persistence.cpp)
pf_add_test(pf_test_concurrency test_concurrency.cpp)
pf_add_test(pf_test_adversarial test_adversarial.cpp)
pf_add_test(pf_test_property test_property.cpp)
pf_add_test(pf_test_scale test_scale.cpp)
pf_add_test(pf_test_platform test_platform.cpp)
pf_add_test(pf_test_distributed test_distributed.cpp)

if(PF_BUILD_TOOLS)
  pf_add_test(pf_test_multiprocess test_multiprocess.cpp process.cpp)
  target_compile_definitions(pf_test_multiprocess PRIVATE
    PF_COORDINATOR_EXE="$<TARGET_FILE:pf_coordinator>"
    PF_WORKER_EXE="$<TARGET_FILE:pf_worker>")
  add_dependencies(pf_test_multiprocess pf_coordinator pf_worker)
endif()
