find_package(benchmark CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(gtosd_benchmark_smoke benchmark_smoke.cpp)
target_link_libraries(gtosd_benchmark_smoke
  PRIVATE
    gtosd::equity
    gtosd::isomorphism
    gtosd::solver
    gtosd::memory
    benchmark::benchmark
    benchmark::benchmark_main)
gtosd_set_warnings(gtosd_benchmark_smoke)

add_executable(gtosd_signed_codec_benchmark signed_codec_benchmark.cpp)
target_link_libraries(gtosd_signed_codec_benchmark
  PRIVATE benchmark::benchmark benchmark::benchmark_main)
gtosd_set_warnings(gtosd_signed_codec_benchmark)

add_executable(gtosd_showdown_fusion_benchmark showdown_fusion_benchmark.cpp)
target_link_libraries(gtosd_showdown_fusion_benchmark
  PRIVATE benchmark::benchmark benchmark::benchmark_main)
gtosd_set_warnings(gtosd_showdown_fusion_benchmark)

add_executable(gtosd_hero_tiled_treelet_benchmark hero_tiled_treelet_benchmark.cpp)
target_link_libraries(gtosd_hero_tiled_treelet_benchmark
  PRIVATE benchmark::benchmark benchmark::benchmark_main)
gtosd_set_warnings(gtosd_hero_tiled_treelet_benchmark)

add_executable(gtosd_exact_state_representation_benchmark
  exact_state_representation_benchmark.cpp)
target_link_libraries(gtosd_exact_state_representation_benchmark
  PRIVATE benchmark::benchmark)
gtosd_set_warnings(gtosd_exact_state_representation_benchmark)

add_executable(gtosd_tile_state_pipeline_benchmark
  tile_state_pipeline_benchmark.cpp)
target_link_libraries(gtosd_tile_state_pipeline_benchmark
  PRIVATE benchmark::benchmark)
gtosd_set_warnings(gtosd_tile_state_pipeline_benchmark)

add_executable(gtosd_real_node_state_replay real_node_state_replay.cpp)
target_link_libraries(gtosd_real_node_state_replay PRIVATE nlohmann_json::nlohmann_json)
gtosd_set_warnings(gtosd_real_node_state_replay)

add_executable(gtosd_river_bucket_qualification river_bucket_qualification.cpp)
target_link_libraries(gtosd_river_bucket_qualification
  PRIVATE
    gtosd::postflop_subgame
    gtosd::best_response
    gtosd::memory
    nlohmann_json::nlohmann_json)
gtosd_set_warnings(gtosd_river_bucket_qualification)

add_executable(gtosd_hu_preflop_reference hu_preflop_reference.cpp)
target_link_libraries(gtosd_hu_preflop_reference
  PRIVATE
    gtosd::core
    nlohmann_json::nlohmann_json)
gtosd_set_warnings(gtosd_hu_preflop_reference)

add_test(NAME gtosd_hu_preflop_reference_preflight
  COMMAND gtosd_hu_preflop_reference
    --fixture ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/hu_preflop_co40_reference_v1.json
    --preflight-only)
set_tests_properties(gtosd_hu_preflop_reference_preflight
  PROPERTIES LABELS "benchmark;preflop;reference;qualification")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_hu_preflop_reference_preflight PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_hu_preflop_tree hu_preflop_tree.cpp)
target_link_libraries(gtosd_hu_preflop_tree
  PRIVATE
    gtosd::preflop
    nlohmann_json::nlohmann_json)
gtosd_set_warnings(gtosd_hu_preflop_tree)
add_test(NAME gtosd_hu_preflop_tree COMMAND gtosd_hu_preflop_tree)
set_tests_properties(gtosd_hu_preflop_tree PROPERTIES
  LABELS "benchmark;preflop;tree;resource")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_hu_preflop_tree PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_hu_preflop_trainer hu_preflop_trainer.cpp)
target_link_libraries(gtosd_hu_preflop_trainer PRIVATE gtosd::preflop_trainer)
gtosd_set_warnings(gtosd_hu_preflop_trainer)
add_test(NAME gtosd_hu_preflop_trainer COMMAND gtosd_hu_preflop_trainer)
set_tests_properties(gtosd_hu_preflop_trainer
  PROPERTIES LABELS "benchmark;preflop;trainer;isolation")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_hu_preflop_trainer PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_hu_preflop_solve hu_preflop_solve.cpp)
target_link_libraries(gtosd_hu_preflop_solve
  PRIVATE
    gtosd::preflop
    nlohmann_json::nlohmann_json)
gtosd_set_warnings(gtosd_hu_preflop_solve)

add_executable(gtosd_hu_preflop_decomposition hu_preflop_decomposition.cpp)
target_link_libraries(gtosd_hu_preflop_decomposition
  PRIVATE
    gtosd::preflop
    gtosd_postflop_preflop_experimental)
gtosd_set_warnings(gtosd_hu_preflop_decomposition)

add_executable(gtosd_hu_preflop_all_in_policy_audit hu_preflop_all_in_policy_audit.cpp)
target_link_libraries(gtosd_hu_preflop_all_in_policy_audit
  PRIVATE
    gtosd::preflop
    nlohmann_json::nlohmann_json)
gtosd_set_warnings(gtosd_hu_preflop_all_in_policy_audit)

add_executable(gtosd_hu_preflop_postflop_query hu_preflop_postflop_query.cpp)
target_link_libraries(gtosd_hu_preflop_postflop_query
  PRIVATE
    gtosd::preflop
    nlohmann_json::nlohmann_json)
gtosd_set_warnings(gtosd_hu_preflop_postflop_query)

add_executable(gtosd_mccfr_nashconv_validation mccfr_nashconv_validation.cpp)
target_link_libraries(gtosd_mccfr_nashconv_validation
  PRIVATE
    gtosd::best_response
    nlohmann_json::nlohmann_json)
gtosd_set_warnings(gtosd_mccfr_nashconv_validation)

add_executable(gtosd_hu_preflop_nashconv_probe hu_preflop_nashconv_probe.cpp)
target_link_libraries(gtosd_hu_preflop_nashconv_probe
  PRIVATE
    gtosd::preflop
    nlohmann_json::nlohmann_json)
gtosd_set_warnings(gtosd_hu_preflop_nashconv_probe)
add_test(NAME gtosd_hu_preflop_decomposition
  COMMAND gtosd_hu_preflop_decomposition)
set_tests_properties(gtosd_hu_preflop_decomposition PROPERTIES
  LABELS "benchmark;preflop;decomposition;resource;qualification")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_hu_preflop_decomposition PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_test(NAME gtosd_river_bucket_qualification_preflight
  COMMAND gtosd_river_bucket_qualification
    --manifest ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/river_bucket_qualification_corpus_v1.json
    --preflight-only)
set_tests_properties(gtosd_river_bucket_qualification_preflight
  PROPERTIES LABELS "benchmark;postflop;river;card_abstraction;qualification")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_river_bucket_qualification_preflight PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_test(NAME gtosd_river_bucket_diagnostic_smoke
  COMMAND gtosd_river_bucket_qualification
    --manifest ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/river_bucket_qualification_corpus_v1.json
    --diagnostic-smoke-only)
set_tests_properties(gtosd_river_bucket_diagnostic_smoke
  PROPERTIES LABELS "benchmark;postflop;river;card_abstraction;oracle;memory")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_river_bucket_diagnostic_smoke PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_test(NAME gtosd_river_exact_blocker_qualification_preflight
  COMMAND gtosd_river_bucket_qualification
    --manifest ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/river_exact_blocker_qualification_corpus_v2.json
    --preflight-only)
set_tests_properties(gtosd_river_exact_blocker_qualification_preflight
  PROPERTIES LABELS "benchmark;postflop;river;card_abstraction;qualification;exact_blocker")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_river_exact_blocker_qualification_preflight PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_test(NAME gtosd_river_joint_equitable_feasibility_preflight
  COMMAND gtosd_river_bucket_qualification
    --manifest ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/river_exact_blocker_qualification_corpus_v2.json
    --equitable-preflight-only)
set_tests_properties(gtosd_river_joint_equitable_feasibility_preflight
  PROPERTIES LABELS "benchmark;postflop;river;card_abstraction;feasibility;equitable")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_river_joint_equitable_feasibility_preflight PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_test(NAME gtosd_river_showdown_distribution_qualification_preflight
  COMMAND gtosd_river_bucket_qualification
    --manifest ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/river_showdown_distribution_qualification_corpus_v3.json
    --preflight-only)
set_tests_properties(gtosd_river_showdown_distribution_qualification_preflight
  PROPERTIES LABELS "benchmark;postflop;river;card_abstraction;qualification;distribution")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_river_showdown_distribution_qualification_preflight PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_test(NAME gtosd_river_showdown_distribution_diagnostic_smoke
  COMMAND gtosd_river_bucket_qualification
    --manifest ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/river_showdown_distribution_qualification_corpus_v3.json
    --diagnostic-smoke-only)
set_tests_properties(gtosd_river_showdown_distribution_diagnostic_smoke
  PROPERTIES LABELS "benchmark;postflop;river;card_abstraction;oracle;memory;distribution")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_river_showdown_distribution_diagnostic_smoke PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_executable(gtosd_fd_ftrl_omd_local_benchmark fd_ftrl_omd_local_benchmark.cpp)
target_link_libraries(gtosd_fd_ftrl_omd_local_benchmark
  PRIVATE benchmark::benchmark)
gtosd_set_warnings(gtosd_fd_ftrl_omd_local_benchmark)

add_test(NAME gtosd_fd_ftrl_omd_local_benchmark_smoke
  COMMAND gtosd_fd_ftrl_omd_local_benchmark
    --benchmark_filter=local/.*_direct
    --benchmark_min_time=0.01s
    --benchmark_repetitions=1)
set_tests_properties(gtosd_fd_ftrl_omd_local_benchmark_smoke
  PROPERTIES LABELS "benchmark;postflop;fd-ftrl;fd-omd;oracle")

add_test(NAME gtosd_exact_state_representation_benchmark_smoke
  COMMAND gtosd_exact_state_representation_benchmark
    --benchmark_min_time=0.01s
    --benchmark_repetitions=1)
set_tests_properties(gtosd_exact_state_representation_benchmark_smoke
  PROPERTIES LABELS "benchmark;postflop;state;phase7")

add_test(NAME gtosd_tile_state_pipeline_benchmark_smoke
  COMMAND gtosd_tile_state_pipeline_benchmark
    --benchmark_min_time=0.01s
    --benchmark_repetitions=1)
set_tests_properties(gtosd_tile_state_pipeline_benchmark_smoke
  PROPERTIES LABELS "benchmark;postflop;state;tile;phase7")

add_test(NAME gtosd_showdown_fusion_benchmark_smoke
  COMMAND gtosd_showdown_fusion_benchmark
    --benchmark_filter=BM_Showdown.*small
    --benchmark_min_time=0.01s
    --benchmark_repetitions=1)
set_tests_properties(gtosd_showdown_fusion_benchmark_smoke
  PROPERTIES LABELS "benchmark;showdown;phase7")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_showdown_fusion_benchmark_smoke PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_test(NAME gtosd_hero_tiled_treelet_benchmark_smoke
  COMMAND gtosd_hero_tiled_treelet_benchmark
    --benchmark_filter=BM_Treelet.*tst_p0
    --benchmark_min_time=0.01s
    --benchmark_repetitions=1)
set_tests_properties(gtosd_hero_tiled_treelet_benchmark_smoke
  PROPERTIES LABELS "benchmark;showdown;treelet;phase7")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_hero_tiled_treelet_benchmark_smoke PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()

add_test(NAME gtosd_benchmark_runner_smoke
  COMMAND gtosd_benchmark_smoke
    --benchmark_filter=BM_EvaluateSeven
    --benchmark_min_time=0.01s
    --benchmark_repetitions=1)
set_tests_properties(gtosd_benchmark_runner_smoke
  PROPERTIES LABELS "benchmark;phase0")
if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
  set_tests_properties(gtosd_benchmark_runner_smoke PROPERTIES
    ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
endif()
