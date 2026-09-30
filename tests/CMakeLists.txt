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

add_executable(gtosd_hu_preflop_tests hu_preflop_tests.cpp)
target_link_libraries(gtosd_hu_preflop_tests PRIVATE gtosd::preflop)
target_compile_definitions(gtosd_hu_preflop_tests PRIVATE
  GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
gtosd_set_warnings(gtosd_hu_preflop_tests)
add_test(NAME gtosd_hu_preflop_tests COMMAND gtosd_hu_preflop_tests)
set_tests_properties(gtosd_hu_preflop_tests
  PROPERTIES LABELS "preflop;tree;resource;reference")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_hu_preflop_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_hu_preflop_trainer_isolation_tests hu_preflop_trainer_isolation_tests.cpp)
target_link_libraries(gtosd_hu_preflop_trainer_isolation_tests PRIVATE gtosd::preflop_trainer)
gtosd_set_warnings(gtosd_hu_preflop_trainer_isolation_tests)
add_test(NAME gtosd_hu_preflop_trainer_isolation_tests
  COMMAND gtosd_hu_preflop_trainer_isolation_tests)
set_tests_properties(gtosd_hu_preflop_trainer_isolation_tests
  PROPERTIES LABELS "preflop;trainer;isolation")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_hu_preflop_trainer_isolation_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_test(NAME gtosd_hu_preflop_trainer_dependency_check
  COMMAND ${CMAKE_COMMAND}
    -DGTOSD_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -P ${PROJECT_SOURCE_DIR}/tests/verify_preflop_trainer_isolation.cmake)
set_tests_properties(gtosd_hu_preflop_trainer_dependency_check
  PROPERTIES LABELS "preflop;trainer;isolation;dependencies")

add_executable(gtosd_external_sampling_tests external_sampling_tests.cpp)
target_link_libraries(gtosd_external_sampling_tests PRIVATE gtosd::best_response)
gtosd_set_warnings(gtosd_external_sampling_tests)
add_test(NAME gtosd_external_sampling_tests COMMAND gtosd_external_sampling_tests)
set_tests_properties(gtosd_external_sampling_tests
  PROPERTIES LABELS "preflop;sampling;averaging;r2;oracle")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_external_sampling_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_hu_preflop_sampling_tests hu_preflop_sampling_tests.cpp)
target_link_libraries(gtosd_hu_preflop_sampling_tests PRIVATE gtosd::preflop_trainer)
gtosd_set_warnings(gtosd_hu_preflop_sampling_tests)
add_test(NAME gtosd_hu_preflop_sampling_tests COMMAND gtosd_hu_preflop_sampling_tests)
set_tests_properties(gtosd_hu_preflop_sampling_tests
  PROPERTIES LABELS "preflop;sampling;averaging;r2")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_hu_preflop_sampling_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_seven_card_table_tests seven_card_table_tests.cpp)
target_link_libraries(gtosd_seven_card_table_tests PRIVATE gtosd::equity gtosd::preflop_trainer)
gtosd_set_warnings(gtosd_seven_card_table_tests)
add_test(NAME gtosd_seven_card_table_tests COMMAND gtosd_seven_card_table_tests)
set_tests_properties(gtosd_seven_card_table_tests
  PROPERTIES LABELS "preflop;evaluator;r3;exhaustive;slow")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_seven_card_table_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_hu_preflop_compiled_tests hu_preflop_compiled_tests.cpp)
target_link_libraries(gtosd_hu_preflop_compiled_tests PRIVATE gtosd::preflop_trainer)
gtosd_set_warnings(gtosd_hu_preflop_compiled_tests)
add_test(NAME gtosd_hu_preflop_compiled_tests COMMAND gtosd_hu_preflop_compiled_tests)
set_tests_properties(gtosd_hu_preflop_compiled_tests
  PROPERTIES LABELS "preflop;compiled;memory;r4")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_hu_preflop_compiled_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_hu_preflop_abstraction_tests hu_preflop_abstraction_tests.cpp)
target_link_libraries(gtosd_hu_preflop_abstraction_tests PRIVATE gtosd::preflop_trainer)
gtosd_set_warnings(gtosd_hu_preflop_abstraction_tests)
add_test(NAME gtosd_hu_preflop_abstraction_tests COMMAND gtosd_hu_preflop_abstraction_tests)
set_tests_properties(gtosd_hu_preflop_abstraction_tests
  PROPERTIES LABELS "preflop;abstraction;persistence;coverage;r5")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_hu_preflop_abstraction_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_hu_preflop_abstract_game_tests hu_preflop_abstract_game_tests.cpp)
target_link_libraries(gtosd_hu_preflop_abstract_game_tests
  PRIVATE gtosd::preflop_certifier gtosd::best_response)
gtosd_set_warnings(gtosd_hu_preflop_abstract_game_tests)
add_test(NAME gtosd_hu_preflop_abstract_game_tests COMMAND gtosd_hu_preflop_abstract_game_tests)
set_tests_properties(gtosd_hu_preflop_abstract_game_tests
  PROPERTIES LABELS "preflop;v23;abstract-game;nashconv;exact")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_hu_preflop_abstract_game_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_hu_preflop_parallel_tests hu_preflop_parallel_tests.cpp)
target_link_libraries(gtosd_hu_preflop_parallel_tests PRIVATE gtosd::preflop_trainer)
gtosd_set_warnings(gtosd_hu_preflop_parallel_tests)
add_test(NAME gtosd_hu_preflop_parallel_tests COMMAND gtosd_hu_preflop_parallel_tests)
set_tests_properties(gtosd_hu_preflop_parallel_tests
  PROPERTIES LABELS "preflop;parallel;deterministic;memory;r6")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_hu_preflop_parallel_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_hu_preflop_telemetry_tests hu_preflop_telemetry_tests.cpp)
target_link_libraries(gtosd_hu_preflop_telemetry_tests PRIVATE gtosd::preflop_trainer)
gtosd_set_warnings(gtosd_hu_preflop_telemetry_tests)
add_test(NAME gtosd_hu_preflop_telemetry_tests COMMAND gtosd_hu_preflop_telemetry_tests)
set_tests_properties(gtosd_hu_preflop_telemetry_tests
  PROPERTIES LABELS "preflop;telemetry;deterministic;non-mutating;r6;v19")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_hu_preflop_telemetry_tests PROPERTIES
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

add_executable(gtosd_enumerated_best_response_tests enumerated_best_response_tests.cpp)
target_link_libraries(gtosd_enumerated_best_response_tests PRIVATE gtosd::best_response)
gtosd_set_warnings(gtosd_enumerated_best_response_tests)
add_test(NAME gtosd_enumerated_best_response_tests COMMAND gtosd_enumerated_best_response_tests)
set_tests_properties(gtosd_enumerated_best_response_tests PROPERTIES
  LABELS "solver;best_response;reference;imperfect_recall")

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

add_executable(gtosd_tre_validation_tests tre_validation_tests.cpp)
target_link_libraries(gtosd_tre_validation_tests PRIVATE gtosd::solver_validation)
gtosd_set_warnings(gtosd_tre_validation_tests)
add_test(NAME gtosd_tre_validation_tests COMMAND gtosd_tre_validation_tests)
set_tests_properties(gtosd_tre_validation_tests
  PROPERTIES LABELS "solver;validation;tre;statistics")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_tre_validation_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_card_abstraction_tests card_abstraction_tests.cpp)
target_link_libraries(gtosd_card_abstraction_tests
  PRIVATE
    gtosd::solver
    gtosd::best_response)
gtosd_set_warnings(gtosd_card_abstraction_tests)
add_test(NAME gtosd_card_abstraction_tests COMMAND gtosd_card_abstraction_tests)
set_tests_properties(gtosd_card_abstraction_tests
  PROPERTIES LABELS "solver;card_abstraction;dcfr;reference")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_card_abstraction_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_subgame_contract_tests subgame_contract_tests.cpp)
target_link_libraries(gtosd_subgame_contract_tests
  PRIVATE
    gtosd::solver
    gtosd::best_response)
gtosd_set_warnings(gtosd_subgame_contract_tests)
add_test(NAME gtosd_subgame_contract_tests COMMAND gtosd_subgame_contract_tests)
set_tests_properties(gtosd_subgame_contract_tests
  PROPERTIES LABELS "solver;subgame;boundary;reference")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_subgame_contract_tests PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_postflop_subgame_tests postflop_subgame_tests.cpp)
target_link_libraries(gtosd_postflop_subgame_tests
  PRIVATE
    gtosd::postflop_subgame
    gtosd::best_response)
gtosd_set_warnings(gtosd_postflop_subgame_tests)
add_test(NAME gtosd_postflop_subgame_tests COMMAND gtosd_postflop_subgame_tests)
set_tests_properties(gtosd_postflop_subgame_tests
  PROPERTIES LABELS "postflop;subgame;boundary;card_abstraction;dcfr;reference")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_postflop_subgame_tests PROPERTIES
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
target_compile_definitions(gtosd_phase10_tests PRIVATE
  GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
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

if(GTOSD_BUILD_PREFLOP_BLUEPRINT)
  add_executable(gtosd_preflop_blueprint_scaffold_tests preflop_blueprint_scaffold_tests.cpp)
  target_link_libraries(gtosd_preflop_blueprint_scaffold_tests
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction)
  target_compile_definitions(gtosd_preflop_blueprint_scaffold_tests PRIVATE
    GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  gtosd_set_warnings(gtosd_preflop_blueprint_scaffold_tests)
  add_test(NAME gtosd_preflop_blueprint_scaffold_tests
    COMMAND gtosd_preflop_blueprint_scaffold_tests)
  set_tests_properties(gtosd_preflop_blueprint_scaffold_tests
    PROPERTIES LABELS "preflop_blueprint;p0")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_scaffold_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()

  add_test(NAME gtosd_preflop_blueprint_dependency_check
    COMMAND ${CMAKE_COMMAND}
      -DGTOSD_SOURCE_DIR=${PROJECT_SOURCE_DIR}
      -P ${PROJECT_SOURCE_DIR}/tests/verify_preflop_blueprint_isolation.cmake)
  set_tests_properties(gtosd_preflop_blueprint_dependency_check
    PROPERTIES LABELS "preflop_blueprint;p0;dependencies")

  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME gtosd_preflop_blueprint_fixture_schema
      COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tools/validate_preflop_blueprint_fixtures.py
        --schema ${PROJECT_SOURCE_DIR}/schemas/preflop_blueprint_game.schema.json
        ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_full_v1.json
        ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json
        ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_co40_v1.json
        ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_co40_test_v1.json
        ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu20_test_v1.json
        ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu30_test_v1.json
        ${PROJECT_SOURCE_DIR}/benchmarks/monker/3WAY50_donk.json
        ${PROJECT_SOURCE_DIR}/benchmarks/monker/3WAY50_donk_allin5x.json
        ${PROJECT_SOURCE_DIR}/benchmarks/monker/3WAY50_donk_rake.json
        ${PROJECT_SOURCE_DIR}/benchmarks/monker/3WAY100_donk.json
        ${PROJECT_SOURCE_DIR}/benchmarks/monker/HU50.json
        ${PROJECT_SOURCE_DIR}/benchmarks/monker/HU50_step2.json
        ${PROJECT_SOURCE_DIR}/benchmarks/monker/HU50_step2_donk.json
        ${PROJECT_SOURCE_DIR}/benchmarks/monker/HU50_step2_allin5x.json
        ${PROJECT_SOURCE_DIR}/benchmarks/monker/HU50_step2_donk_allin5x.json
        ${PROJECT_SOURCE_DIR}/benchmarks/monker/HU50_rake.json
        ${PROJECT_SOURCE_DIR}/benchmarks/monker/HU50_step2_donk_rake.json
        ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_rake_v1.json)
    set_tests_properties(gtosd_preflop_blueprint_fixture_schema
      PROPERTIES LABELS "preflop_blueprint;p0;schema" SKIP_RETURN_CODE 77)
  else()
    message(STATUS "Python3 not found: preflop blueprint fixture schema test not registered")
  endif()
endif()

if(GTOSD_BUILD_PREFLOP_BLUEPRINT)
  add_executable(gtosd_card_abstraction_canonical_tests card_abstraction_canonical_tests.cpp)
  target_link_libraries(gtosd_card_abstraction_canonical_tests PRIVATE gtosd::card_abstraction)
  gtosd_set_warnings(gtosd_card_abstraction_canonical_tests)
  add_test(NAME gtosd_card_abstraction_canonical_tests
    COMMAND gtosd_card_abstraction_canonical_tests)
  set_tests_properties(gtosd_card_abstraction_canonical_tests
    PROPERTIES LABELS "preflop_blueprint;p1;canonical")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_card_abstraction_canonical_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
endif()

if(GTOSD_BUILD_PREFLOP_BLUEPRINT)
  add_executable(gtosd_card_abstraction_features_tests card_abstraction_features_tests.cpp)
  target_link_libraries(gtosd_card_abstraction_features_tests PRIVATE gtosd::card_abstraction)
  gtosd_set_warnings(gtosd_card_abstraction_features_tests)
  add_test(NAME gtosd_card_abstraction_features_tests
    COMMAND gtosd_card_abstraction_features_tests)
  set_tests_properties(gtosd_card_abstraction_features_tests
    PROPERTIES LABELS "preflop_blueprint;p2;features")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_card_abstraction_features_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
endif()

if(GTOSD_BUILD_PREFLOP_BLUEPRINT)
  add_executable(gtosd_card_abstraction_bucket_tests card_abstraction_bucket_tests.cpp)
  target_link_libraries(gtosd_card_abstraction_bucket_tests PRIVATE gtosd::card_abstraction)
  gtosd_set_warnings(gtosd_card_abstraction_bucket_tests)
  add_test(NAME gtosd_card_abstraction_bucket_tests
    COMMAND gtosd_card_abstraction_bucket_tests)
  set_tests_properties(gtosd_card_abstraction_bucket_tests
    PROPERTIES LABELS "preflop_blueprint;p3;buckets")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_card_abstraction_bucket_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
endif()

if(GTOSD_BUILD_PREFLOP_BLUEPRINT)
  add_executable(gtosd_card_abstraction_three_way_tests card_abstraction_three_way_tests.cpp)
  target_link_libraries(gtosd_card_abstraction_three_way_tests PRIVATE gtosd::card_abstraction)
  gtosd_set_warnings(gtosd_card_abstraction_three_way_tests)
  # The rank and heads-up tables are built when the P2 resources lack them, so
  # V6 always runs; when the resources hold the complete table
  # (preflop_three_way_v1.bin) it is loaded and its rows compared with the subset.
  add_test(NAME gtosd_card_abstraction_three_way_tests
    COMMAND gtosd_card_abstraction_three_way_tests
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources)
  set_tests_properties(gtosd_card_abstraction_three_way_tests
    PROPERTIES LABELS "preflop_blueprint;three_way;card_abstraction")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_card_abstraction_three_way_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
endif()

if(GTOSD_BUILD_PREFLOP_BLUEPRINT)
  add_executable(gtosd_preflop_blueprint_game_tests preflop_blueprint_game_tests.cpp)
  target_link_libraries(gtosd_preflop_blueprint_game_tests
    PRIVATE gtosd::preflop_blueprint gtosd::core)
  target_compile_definitions(gtosd_preflop_blueprint_game_tests PRIVATE
    GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  gtosd_set_warnings(gtosd_preflop_blueprint_game_tests)
  add_test(NAME gtosd_preflop_blueprint_game_tests COMMAND gtosd_preflop_blueprint_game_tests)
  set_tests_properties(gtosd_preflop_blueprint_game_tests
    PROPERTIES LABELS "preflop_blueprint;p4;game")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_game_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
endif()

if(GTOSD_BUILD_PREFLOP_BLUEPRINT)
  add_executable(gtosd_preflop_blueprint_kernel_tests preflop_blueprint_kernel_tests.cpp)
  target_link_libraries(gtosd_preflop_blueprint_kernel_tests
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction gtosd::core)
  target_compile_definitions(gtosd_preflop_blueprint_kernel_tests PRIVATE
    GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  gtosd_set_warnings(gtosd_preflop_blueprint_kernel_tests)
  # The exact rank and all-in tables are loaded from the P2 output directory
  # when present and rebuilt otherwise; bucket tables are optional.
  add_test(NAME gtosd_preflop_blueprint_kernel_tests
    COMMAND gtosd_preflop_blueprint_kernel_tests
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000)
  set_tests_properties(gtosd_preflop_blueprint_kernel_tests
    PROPERTIES LABELS "preflop_blueprint;p5;kernels")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_kernel_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
endif()

if(GTOSD_BUILD_PREFLOP_BLUEPRINT)
  # Oracle: the standalone postflop solver is linked by this test only (D5).
  add_executable(gtosd_preflop_blueprint_oracle_tests preflop_blueprint_oracle_tests.cpp)
  target_link_libraries(gtosd_preflop_blueprint_oracle_tests
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction gtosd::postflop gtosd::tree
            gtosd::core)
  target_compile_definitions(gtosd_preflop_blueprint_oracle_tests PRIVATE
    GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  gtosd_set_warnings(gtosd_preflop_blueprint_oracle_tests)
  add_test(NAME gtosd_preflop_blueprint_oracle_tests
    COMMAND gtosd_preflop_blueprint_oracle_tests
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources)
  set_tests_properties(gtosd_preflop_blueprint_oracle_tests
    PROPERTIES LABELS "preflop_blueprint;p5;oracle")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_oracle_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
endif()

if(GTOSD_BUILD_PREFLOP_BLUEPRINT)
  # Oracle: the FiniteGame solver is linked by this test only (roadmap 2.2).
  add_executable(gtosd_preflop_blueprint_trainer_tests preflop_blueprint_trainer_tests.cpp)
  target_link_libraries(gtosd_preflop_blueprint_trainer_tests
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction gtosd::solver gtosd::best_response
            gtosd::core)
  target_compile_definitions(gtosd_preflop_blueprint_trainer_tests PRIVATE
    GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  gtosd_set_warnings(gtosd_preflop_blueprint_trainer_tests)
  add_test(NAME gtosd_preflop_blueprint_trainer_tests
    COMMAND gtosd_preflop_blueprint_trainer_tests
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000)
  set_tests_properties(gtosd_preflop_blueprint_trainer_tests
    PROPERTIES LABELS "preflop_blueprint;p6;trainer")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_trainer_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()

  # Preflop lock of the trainer and its chart helper (benchmarks/monker_chart_lock.hpp).
  add_executable(gtosd_preflop_blueprint_preflop_lock_tests
    preflop_blueprint_preflop_lock_tests.cpp)
  target_link_libraries(gtosd_preflop_blueprint_preflop_lock_tests
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction gtosd::solver gtosd::best_response
            gtosd::core)
  target_compile_definitions(gtosd_preflop_blueprint_preflop_lock_tests PRIVATE
    GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  gtosd_set_warnings(gtosd_preflop_blueprint_preflop_lock_tests)
  add_test(NAME gtosd_preflop_blueprint_preflop_lock_tests
    COMMAND gtosd_preflop_blueprint_preflop_lock_tests
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000)
  set_tests_properties(gtosd_preflop_blueprint_preflop_lock_tests
    PROPERTIES LABELS "preflop_blueprint;research;monker")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_preflop_lock_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()

  # Board texture maps of the board class rows (benchmarks/monker/textures).
  add_executable(gtosd_preflop_blueprint_board_texture_tests
    preflop_blueprint_board_texture_tests.cpp)
  target_link_libraries(gtosd_preflop_blueprint_board_texture_tests
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction gtosd::solver gtosd::best_response
            gtosd::core)
  target_compile_definitions(gtosd_preflop_blueprint_board_texture_tests PRIVATE
    GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  gtosd_set_warnings(gtosd_preflop_blueprint_board_texture_tests)
  add_test(NAME gtosd_preflop_blueprint_board_texture_tests
    COMMAND gtosd_preflop_blueprint_board_texture_tests
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --scratch-dir ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_board_texture_scratch)
  set_tests_properties(gtosd_preflop_blueprint_board_texture_tests
    PROPERTIES LABELS "preflop_blueprint;research;monker;texture")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_board_texture_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()

  add_executable(gtosd_preflop_blueprint_certifier_tests preflop_blueprint_certifier_tests.cpp)
  add_executable(gtosd_preflop_blueprint_decision_gap_tests preflop_blueprint_decision_gap_tests.cpp)
  add_executable(gtosd_preflop_blueprint_temporal_distance_tests
    preflop_blueprint_temporal_distance_tests.cpp)
  add_executable(gtosd_preflop_blueprint_temporal_game_tests
    preflop_blueprint_temporal_game_tests.cpp)
  target_link_libraries(gtosd_preflop_blueprint_temporal_game_tests
    PRIVATE gtosd::card_abstraction gtosd::solver gtosd::best_response)
  gtosd_set_warnings(gtosd_preflop_blueprint_temporal_game_tests)
  add_test(NAME gtosd_preflop_blueprint_temporal_game_tests
    COMMAND gtosd_preflop_blueprint_temporal_game_tests)
  target_link_libraries(gtosd_preflop_blueprint_temporal_distance_tests
    PRIVATE gtosd::card_abstraction)
  gtosd_set_warnings(gtosd_preflop_blueprint_temporal_distance_tests)
  add_test(NAME gtosd_preflop_blueprint_temporal_distance_tests
    COMMAND gtosd_preflop_blueprint_temporal_distance_tests)
  target_link_libraries(gtosd_preflop_blueprint_decision_gap_tests PRIVATE gtosd::preflop_blueprint)
  gtosd_set_warnings(gtosd_preflop_blueprint_decision_gap_tests)
  add_test(NAME gtosd_preflop_blueprint_decision_gap_tests
    COMMAND gtosd_preflop_blueprint_decision_gap_tests)
  if(Python3_Interpreter_FOUND AND GTOSD_BUILD_BENCHMARKS)
    add_test(NAME gtosd_preflop_blueprint_temporal_controls_oracle
      COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/preflop_blueprint_temporal_controls_oracle.py
        --executable $<TARGET_FILE:gtosd_preflop_blueprint_temporal_witness>
        --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
        --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
        --scratch-dir ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_temporal_controls_scratch)
    set_tests_properties(gtosd_preflop_blueprint_temporal_controls_oracle PROPERTIES
      LABELS "preflop_blueprint;research;temporal")
    if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
      set_tests_properties(gtosd_preflop_blueprint_temporal_controls_oracle PROPERTIES
        ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
    endif()
  endif()
  target_link_libraries(gtosd_preflop_blueprint_certifier_tests
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction gtosd::solver gtosd::best_response
            gtosd::core)
  target_compile_definitions(gtosd_preflop_blueprint_certifier_tests PRIVATE
    GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  gtosd_set_warnings(gtosd_preflop_blueprint_certifier_tests)
  add_test(NAME gtosd_preflop_blueprint_certifier_tests
    COMMAND gtosd_preflop_blueprint_certifier_tests
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --scratch-dir ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_certifier_scratch)
  set_tests_properties(gtosd_preflop_blueprint_certifier_tests
    PROPERTIES LABELS "preflop_blueprint;p7;certifier")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_certifier_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()

  find_package(nlohmann_json CONFIG REQUIRED)
  add_executable(gtosd_preflop_blueprint_export_tests preflop_blueprint_export_tests.cpp)
  target_link_libraries(gtosd_preflop_blueprint_export_tests
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction gtosd::solver gtosd::best_response
            gtosd::core nlohmann_json::nlohmann_json)
  target_compile_definitions(gtosd_preflop_blueprint_export_tests PRIVATE
    GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  gtosd_set_warnings(gtosd_preflop_blueprint_export_tests)
  add_test(NAME gtosd_preflop_blueprint_export_tests
    COMMAND gtosd_preflop_blueprint_export_tests
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --scratch-dir ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_export_scratch)
  set_tests_properties(gtosd_preflop_blueprint_export_tests
    PROPERTIES LABELS "preflop_blueprint;p8;export")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_export_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
  add_executable(gtosd_preflop_blueprint_monker_values_tests
    preflop_blueprint_monker_values_tests.cpp)
  target_link_libraries(gtosd_preflop_blueprint_monker_values_tests
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction gtosd::solver gtosd::best_response
            gtosd::core)
  target_compile_definitions(gtosd_preflop_blueprint_monker_values_tests PRIVATE
    GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  gtosd_set_warnings(gtosd_preflop_blueprint_monker_values_tests)
  add_test(NAME gtosd_preflop_blueprint_monker_values_tests
    COMMAND gtosd_preflop_blueprint_monker_values_tests
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --scratch-dir ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_monker_values_scratch)
  set_tests_properties(gtosd_preflop_blueprint_monker_values_tests
    PROPERTIES LABELS "preflop_blueprint;research;monker")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_monker_values_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
  # Exact-flop per-board bucket tables (gtosd_preflop_blueprint_monker_buckets
  # --flop-exact, written by the benchmarks fixture): suit orbits and ids on
  # named and on all canonical flops, the 15x4 turn and 15-level river
  # fingerprints of the non-exact run, the board class rows with TX2.
  add_executable(gtosd_preflop_blueprint_monker_buckets_tests
    preflop_blueprint_monker_buckets_tests.cpp)
  target_link_libraries(gtosd_preflop_blueprint_monker_buckets_tests
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction gtosd::solver gtosd::best_response
            gtosd::core)
  target_compile_definitions(gtosd_preflop_blueprint_monker_buckets_tests PRIVATE
    GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  gtosd_set_warnings(gtosd_preflop_blueprint_monker_buckets_tests)
  if(GTOSD_BUILD_BENCHMARKS)
    add_test(NAME gtosd_preflop_blueprint_monker_buckets_tests
      COMMAND gtosd_preflop_blueprint_monker_buckets_tests
        --buckets-dir ${PROJECT_BINARY_DIR}/benchmarks/preflop_blueprint_monker_buckets_exact
        --texture-map
          ${PROJECT_SOURCE_DIR}/benchmarks/monker/textures/texture_map_TX2_recommended.txt
        --expect-flop fnv1a64:430aa1ec4a8d2307
        --expect-turn fnv1a64:e03b8a4c8f3e91e7 --expect-river fnv1a64:e819b14897bca674
        --street-settings present)
    set_tests_properties(gtosd_preflop_blueprint_monker_buckets_tests
      PROPERTIES LABELS "preflop_blueprint;research;monker"
                 FIXTURES_REQUIRED preflop_blueprint_monker_buckets_exact)
    # Byte identity of the non-exact paths with the tables of the builder before
    # per-street settings (out/monker/buckets_15x4 and buckets_30x4 fingerprints).
    add_test(NAME gtosd_preflop_blueprint_monker_buckets_default_tests
      COMMAND gtosd_preflop_blueprint_monker_buckets_tests
        --buckets-dir ${PROJECT_BINARY_DIR}/benchmarks/preflop_blueprint_monker_buckets_default
        --expect-flop fnv1a64:cf2607c75869355d --expect-turn fnv1a64:e03b8a4c8f3e91e7
        --expect-river fnv1a64:e819b14897bca674 --street-settings absent)
    set_tests_properties(gtosd_preflop_blueprint_monker_buckets_default_tests
      PROPERTIES LABELS "preflop_blueprint;research;monker"
                 FIXTURES_REQUIRED preflop_blueprint_monker_buckets_default)
    add_test(NAME gtosd_preflop_blueprint_monker_buckets_streets_tests
      COMMAND gtosd_preflop_blueprint_monker_buckets_tests
        --buckets-dir ${PROJECT_BINARY_DIR}/benchmarks/preflop_blueprint_monker_buckets_streets
        --expect-flop fnv1a64:cf2607c75869355d --expect-turn fnv1a64:575640ec13251aa5
        --expect-river fnv1a64:ebab0a9cf4c56cc8 --street-settings present)
    set_tests_properties(gtosd_preflop_blueprint_monker_buckets_streets_tests
      PROPERTIES LABELS "preflop_blueprint;research;monker"
                 FIXTURES_REQUIRED preflop_blueprint_monker_buckets_streets)
    if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
      foreach(checker gtosd_preflop_blueprint_monker_buckets_tests
                      gtosd_preflop_blueprint_monker_buckets_default_tests
                      gtosd_preflop_blueprint_monker_buckets_streets_tests)
        set_tests_properties(${checker} PROPERTIES
          ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
      endforeach()
    endif()
  endif()
  if(Python3_Interpreter_FOUND AND GTOSD_BUILD_BENCHMARKS)
    # The Python recursion over the smoke JSON: our own rows reproduce the
    # tool's EV, the chart set of the smoke policy its loss.
    add_test(NAME gtosd_preflop_blueprint_monker_in_our_game_smoke
      COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tools/monker_compare/monker_in_our_game.py
        ${PROJECT_BINARY_DIR}/benchmarks/preflop_blueprint_monker_values_smoke.json
        --monker ${PROJECT_BINARY_DIR}/benchmarks/preflop_blueprint_monker_charts_smoke
        --ours ${PROJECT_BINARY_DIR}/benchmarks/preflop_blueprint_monker_charts_smoke
        --check)
    set_tests_properties(gtosd_preflop_blueprint_monker_in_our_game_smoke
      PROPERTIES LABELS "preflop_blueprint;research;monker"
                 FIXTURES_REQUIRED preflop_blueprint_smoke_monker_values)
    # The same recursion on the game with rake (not zero-sum): every check is
    # per player.
    add_test(NAME gtosd_preflop_blueprint_monker_in_our_game_rake_smoke
      COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tools/monker_compare/monker_in_our_game.py
        ${PROJECT_BINARY_DIR}/benchmarks/preflop_blueprint_monker_values_rake_smoke.json
        --monker ${PROJECT_BINARY_DIR}/benchmarks/preflop_blueprint_monker_charts_rake_smoke
        --ours ${PROJECT_BINARY_DIR}/benchmarks/preflop_blueprint_monker_charts_rake_smoke
        --check)
    set_tests_properties(gtosd_preflop_blueprint_monker_in_our_game_rake_smoke
      PROPERTIES LABELS "preflop_blueprint;research;monker"
                 FIXTURES_REQUIRED preflop_blueprint_rake_smoke_monker_values)
  endif()
  if(Python3_Interpreter_FOUND)
    add_test(NAME gtosd_preflop_blueprint_chart_schema
      COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tools/validate_preflop_blueprint_chart.py
        ${PROJECT_BINARY_DIR}/benchmarks/preflop_blueprint_export_smoke_chart.json)
    set_tests_properties(gtosd_preflop_blueprint_chart_schema
      PROPERTIES LABELS "preflop_blueprint;p8;schema" SKIP_RETURN_CODE 77
                 FIXTURES_REQUIRED preflop_blueprint_smoke_chart)
  endif()
endif()
