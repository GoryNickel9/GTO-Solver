add_executable(gtosd_tests core_tests.cpp)
target_link_libraries(gtosd_tests PRIVATE gtosd::core gtosd::equity)
gtosd_set_warnings(gtosd_tests)
add_test(NAME gtosd_core_tests COMMAND gtosd_tests)
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_core_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_phase1_tests phase1_tests.cpp)
target_link_libraries(gtosd_phase1_tests PRIVATE gtosd::core)
gtosd_set_warnings(gtosd_phase1_tests)
add_test(NAME gtosd_phase1_tests COMMAND gtosd_phase1_tests)
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_phase1_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_phase2_tests phase2_tests.cpp)
target_link_libraries(gtosd_phase2_tests PRIVATE gtosd::core gtosd::equity)
gtosd_set_warnings(gtosd_phase2_tests)
add_test(NAME gtosd_phase2_tests COMMAND gtosd_phase2_tests)
set_tests_properties(gtosd_phase2_tests PROPERTIES LABELS "phase2;oracle")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_phase2_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_phase3_tests phase3_tests.cpp)
target_link_libraries(gtosd_phase3_tests PRIVATE gtosd::tree)
target_compile_definitions(gtosd_phase3_tests PRIVATE
  GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
gtosd_set_warnings(gtosd_phase3_tests)
add_test(NAME gtosd_phase3_tests COMMAND gtosd_phase3_tests)
set_tests_properties(gtosd_phase3_tests PROPERTIES LABELS "phase3;tree")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_phase3_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_phase4_tests phase4_tests.cpp)
target_link_libraries(gtosd_phase4_tests PRIVATE gtosd::isomorphism)
gtosd_set_warnings(gtosd_phase4_tests)
add_test(NAME gtosd_phase4_tests COMMAND gtosd_phase4_tests)
set_tests_properties(gtosd_phase4_tests PROPERTIES LABELS "phase4;isomorphism;exhaustive")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_phase4_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_phase5_tests phase5_tests.cpp)
target_link_libraries(gtosd_phase5_tests
  PRIVATE
    gtosd::solver
    gtosd::best_response)
gtosd_set_warnings(gtosd_phase5_tests)
add_test(NAME gtosd_phase5_tests COMMAND gtosd_phase5_tests)
set_tests_properties(gtosd_phase5_tests
  PROPERTIES LABELS "phase5;solver;best_response;reference")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_phase5_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_phase6_tests phase6_tests.cpp)
target_compile_definitions(gtosd_phase6_tests PRIVATE
  GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(gtosd_phase6_tests
  PRIVATE
    gtosd::memory
    gtosd::best_response)
gtosd_set_warnings(gtosd_phase6_tests)
add_test(NAME gtosd_phase6_tests COMMAND gtosd_phase6_tests)
set_tests_properties(gtosd_phase6_tests
  PROPERTIES LABELS "phase6;memory;exact")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_phase6_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

if(GTOSD_BUILD_NIGHTLY_TESTS)
  add_executable(gtosd_phase2_nightly_tests phase2_nightly_tests.cpp)
  target_link_libraries(gtosd_phase2_nightly_tests PRIVATE gtosd::equity)
  gtosd_set_warnings(gtosd_phase2_nightly_tests)
  add_test(NAME gtosd_phase2_nightly_tests COMMAND gtosd_phase2_nightly_tests)
  set_tests_properties(gtosd_phase2_nightly_tests PROPERTIES LABELS "phase2;nightly")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_phase2_nightly_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
endif()

if(GTOSD_BUILD_FRAMEWORK_TESTS)
  find_package(GTest CONFIG REQUIRED)
  include(GoogleTest)
  add_executable(gtosd_framework_tests framework_tests.cpp)
  target_link_libraries(gtosd_framework_tests PRIVATE gtosd::core GTest::gtest_main)
  gtosd_set_warnings(gtosd_framework_tests)
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    gtest_discover_tests(gtosd_framework_tests
      DISCOVERY_TIMEOUT 30
      PROPERTIES
        ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  else()
    gtest_discover_tests(gtosd_framework_tests
      DISCOVERY_TIMEOUT 30)
  endif()
endif()
