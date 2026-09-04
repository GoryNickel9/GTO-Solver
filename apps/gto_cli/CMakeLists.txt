add_executable(gto_cli main.cpp)
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(gto_cli
  PRIVATE
    gtosd::core
    gtosd::tree
    gtosd::isomorphism
    gtosd::solver
    gtosd::best_response
    gtosd::memory
    gtosd::postflop
    gtosd::storage
    nlohmann_json::nlohmann_json)

# The CLI also hosts compute-only commands which do not touch persistence.
# Avoid charging those paths for loading the storage DLLs at process startup;
# Windows resolves them transparently on the first storage call instead.
if(MSVC)
  target_link_libraries(gto_cli PRIVATE delayimp)
  target_link_options(gto_cli PRIVATE
    /DELAYLOAD:libsodium.dll
    /DELAYLOAD:sqlite3.dll
    /DELAYLOAD:zstd.dll)
endif()
gtosd_set_warnings(gto_cli)

add_test(NAME gtosd_bucketed_postflop_cli
  COMMAND ${CMAKE_COMMAND}
    -DGTOSD_CLI=$<TARGET_FILE:gto_cli>
    -DGTOSD_FIXTURE=${PROJECT_SOURCE_DIR}/tests/fixtures/postflop_fixed_river_bet.json
    -DGTOSD_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -P ${PROJECT_SOURCE_DIR}/tests/verify_bucketed_postflop_cli.cmake)
set_tests_properties(gtosd_bucketed_postflop_cli
  PROPERTIES LABELS "postflop;abstraction;bucketing;cfr-plus;cli;production;exact-br")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_bucketed_postflop_cli PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_test(NAME gtosd_bucketed_subgame_cli
  COMMAND ${CMAKE_COMMAND}
    -DGTOSD_CLI=$<TARGET_FILE:gto_cli>
    -DGTOSD_FIXTURE=${PROJECT_SOURCE_DIR}/tests/fixtures/postflop_fixed_river_bet.json
    -DGTOSD_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -P ${PROJECT_SOURCE_DIR}/tests/verify_bucketed_subgame_cli.cmake)
set_tests_properties(gtosd_bucketed_subgame_cli
  PROPERTIES LABELS "postflop;abstraction;bucketing;subgame;cfr-plus;cli;production;exact-br")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_bucketed_subgame_cli PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_test(NAME gtosd_gto_plus_action_catalog_smoke
  COMMAND ${CMAKE_COMMAND}
    -DGTOSD_CLI=$<TARGET_FILE:gto_cli>
    -DGTOSD_FIXTURE=${PROJECT_SOURCE_DIR}/benchmarks/fixtures/gto_plus_ahkhqh_101.json
    -DGTOSD_REPORT=${CMAKE_CURRENT_BINARY_DIR}/gto_plus_action_catalog_smoke.json
    -P ${PROJECT_SOURCE_DIR}/tests/verify_gto_plus_action_catalog.cmake)
set_tests_properties(gtosd_gto_plus_action_catalog_smoke
  PROPERTIES LABELS "phase10;postflop;gto-plus;cli;diagnostic")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_gto_plus_action_catalog_smoke PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_test(NAME gtosd_tstc9d_canonical_layout
  COMMAND ${CMAKE_COMMAND}
    -DGTOSD_CLI=$<TARGET_FILE:gto_cli>
    -DGTOSD_FIXTURE=${PROJECT_SOURCE_DIR}/benchmarks/fixtures/gto_plus_tstc9d_101.json
    -DGTOSD_REPORT=${CMAKE_CURRENT_BINARY_DIR}/tstc9d_canonical_layout.json
    -P ${PROJECT_SOURCE_DIR}/tests/verify_tstc9d_canonical_layout.cmake)
set_tests_properties(gtosd_tstc9d_canonical_layout
  PROPERTIES LABELS "phase10;postflop;gto-plus;memory;canonical;exact")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_tstc9d_canonical_layout PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_test(NAME gtosd_bucketed_preflight_cli
  COMMAND ${CMAKE_COMMAND}
    -DGTOSD_CLI=$<TARGET_FILE:gto_cli>
    -DGTOSD_TST_FIXTURE=${PROJECT_SOURCE_DIR}/benchmarks/fixtures/gto_plus_tstc9d_101.json
    -DGTOSD_AHK_FIXTURE=${PROJECT_SOURCE_DIR}/benchmarks/fixtures/gto_plus_ahkhqh_101.json
    -DGTOSD_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -P ${PROJECT_SOURCE_DIR}/tests/verify_bucketed_preflight_cli.cmake)
set_tests_properties(gtosd_bucketed_preflight_cli
  PROPERTIES LABELS "postflop;abstraction;bucketing;preflight;gto-plus;production")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_bucketed_preflight_cli PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_test(NAME gtosd_production_dcfr_contract
  COMMAND ${CMAKE_COMMAND}
    -DGTOSD_FIXTURE_DIR=${PROJECT_SOURCE_DIR}/benchmarks/fixtures
    -DGTOSD_RUNNER=${PROJECT_SOURCE_DIR}/tools/run_gto_plus_convergence_benchmark.ps1
    -P ${PROJECT_SOURCE_DIR}/tests/verify_production_dcfr_contract.cmake)
set_tests_properties(gtosd_production_dcfr_contract
  PROPERTIES LABELS "phase10;postflop;gto-plus;contract")
