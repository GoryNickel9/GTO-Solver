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

add_executable(gtosd_phase7_tests phase7_tests.cpp)
target_compile_definitions(gtosd_phase7_tests PRIVATE
  GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
if(GTOSD_ENABLE_PURE_CFR_TRAJECTORY_PROBE)
  target_compile_definitions(gtosd_phase7_tests
    PRIVATE GTOSD_ENABLE_PURE_CFR_TRAJECTORY_PROBE=1)
endif()
target_link_libraries(gtosd_phase7_tests PRIVATE gtosd::postflop)
gtosd_set_warnings(gtosd_phase7_tests)
add_test(NAME gtosd_phase7_tests COMMAND gtosd_phase7_tests)
set_tests_properties(gtosd_phase7_tests
  PROPERTIES LABELS "phase7;postflop;production;exact")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_phase7_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_phase8_tests phase8_tests.cpp)
target_compile_definitions(gtosd_phase8_tests PRIVATE
  GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(gtosd_phase8_tests PRIVATE gtosd::storage)
gtosd_set_warnings(gtosd_phase8_tests)
add_test(NAME gtosd_phase8_tests COMMAND gtosd_phase8_tests)
set_tests_properties(gtosd_phase8_tests
  PROPERTIES LABELS "phase8;storage;encryption;random_access")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_phase8_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_phase8_large_file_tests phase8_large_file_tests.cpp)
target_link_libraries(gtosd_phase8_large_file_tests PRIVATE gtosd::storage)
gtosd_set_warnings(gtosd_phase8_large_file_tests)
if(GTOSD_BUILD_NIGHTLY_TESTS)
  add_test(NAME gtosd_phase8_large_file_tests
    COMMAND gtosd_phase8_large_file_tests)
  set_tests_properties(gtosd_phase8_large_file_tests
    PROPERTIES LABELS "phase8;storage;large;nightly")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_phase8_large_file_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
endif()

add_executable(gtosd_phase9_tests phase9_tests.cpp)
target_link_libraries(gtosd_phase9_tests PRIVATE gtosd::gui_prototype)
gtosd_set_warnings(gtosd_phase9_tests)
add_test(NAME gtosd_phase9_tests COMMAND gtosd_phase9_tests)
set_tests_properties(gtosd_phase9_tests
  PROPERTIES LABELS "phase9;gui;virtualization;d3d11;workflow")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_phase9_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_phase10_tests phase10_tests.cpp)
target_link_libraries(gtosd_phase10_tests PRIVATE gtosd::storage)
if(GTOSD_ENABLE_REAL_NODE_REPLAY)
  target_compile_definitions(gtosd_phase10_tests PRIVATE GTOSD_ENABLE_REAL_NODE_REPLAY=1)
endif()
gtosd_set_warnings(gtosd_phase10_tests)
add_test(NAME gtosd_phase10_tests COMMAND gtosd_phase10_tests)
set_tests_properties(gtosd_phase10_tests
  PROPERTIES LABELS "phase10;postflop;ranges;storage;regression")

add_executable(gtosd_predictive_cfr_oracle_tests predictive_cfr_oracle_tests.cpp)
gtosd_set_warnings(gtosd_predictive_cfr_oracle_tests)
add_test(NAME gtosd_predictive_cfr_oracle_tests COMMAND gtosd_predictive_cfr_oracle_tests)
set_tests_properties(gtosd_predictive_cfr_oracle_tests
  PROPERTIES LABELS "postflop;predictive-cfr;oracle")

add_executable(gtosd_fd_ftrl_omd_oracle_tests fd_ftrl_omd_oracle_tests.cpp)
gtosd_set_warnings(gtosd_fd_ftrl_omd_oracle_tests)
add_test(NAME gtosd_fd_ftrl_omd_oracle_tests COMMAND gtosd_fd_ftrl_omd_oracle_tests)
set_tests_properties(gtosd_fd_ftrl_omd_oracle_tests
  PROPERTIES LABELS "postflop;fd-ftrl;fd-omd;oracle")

add_executable(gtosd_pure_cfr_recheck_oracle_tests pure_cfr_recheck_oracle_tests.cpp)
target_link_libraries(gtosd_pure_cfr_recheck_oracle_tests
  PRIVATE
    gtosd::solver
    gtosd::best_response)
gtosd_set_warnings(gtosd_pure_cfr_recheck_oracle_tests)
add_test(NAME gtosd_pure_cfr_recheck_oracle_tests COMMAND gtosd_pure_cfr_recheck_oracle_tests)
set_tests_properties(gtosd_pure_cfr_recheck_oracle_tests
  PROPERTIES LABELS "solver;pure-cfr;sync-pcfr;oracle;exact")

add_executable(gtosd_gto_plus_reference_tests gto_plus_reference_tests.cpp)
target_link_libraries(gtosd_gto_plus_reference_tests PRIVATE gtosd::postflop)
gtosd_set_warnings(gtosd_gto_plus_reference_tests)
add_test(NAME gtosd_gto_plus_reference_tests COMMAND gtosd_gto_plus_reference_tests)
set_tests_properties(gtosd_gto_plus_reference_tests
  PROPERTIES LABELS "phase10;postflop;ranges;memory;gto-plus;regression")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_phase10_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_solver_memory_ledger_tests solver_memory_ledger_tests.cpp)
target_link_libraries(gtosd_solver_memory_ledger_tests PRIVATE gtosd::postflop)
gtosd_set_warnings(gtosd_solver_memory_ledger_tests)
add_test(NAME gtosd_solver_memory_ledger_tests COMMAND gtosd_solver_memory_ledger_tests)
set_tests_properties(gtosd_solver_memory_ledger_tests
  PROPERTIES LABELS "postflop;memory;ledger;exact")

if(GTOSD_BUILD_RANGE_ORBIT_ORACLE)
  add_executable(gtosd_range_orbit_oracle_tests range_orbit_oracle_tests.cpp)
  target_link_libraries(gtosd_range_orbit_oracle_tests
    PRIVATE gtosd_postflop_range_orbit_oracle)
  gtosd_set_warnings(gtosd_range_orbit_oracle_tests)
  add_test(NAME gtosd_range_orbit_oracle_tests COMMAND gtosd_range_orbit_oracle_tests)
  set_tests_properties(gtosd_range_orbit_oracle_tests
    PROPERTIES LABELS "postflop;ranges;isomorphism;oracle;counterexample")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_range_orbit_oracle_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
endif()

add_executable(gtosd_canonical_layout_tests canonical_layout_tests.cpp)
target_link_libraries(gtosd_canonical_layout_tests PRIVATE gtosd::postflop)
gtosd_set_warnings(gtosd_canonical_layout_tests)
add_test(NAME gtosd_canonical_layout_tests COMMAND gtosd_canonical_layout_tests)
set_tests_properties(gtosd_canonical_layout_tests
  PROPERTIES LABELS "phase10;postflop;memory;canonical;exact")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_canonical_layout_tests PROPERTIES
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
