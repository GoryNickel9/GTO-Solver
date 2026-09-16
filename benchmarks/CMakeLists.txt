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

add_executable(gtosd_hu_preflop_abstract_nashconv hu_preflop_abstract_nashconv.cpp)
target_link_libraries(gtosd_hu_preflop_abstract_nashconv
  PRIVATE
    gtosd::preflop_certifier
    gtosd::best_response
    nlohmann_json::nlohmann_json)
gtosd_set_warnings(gtosd_hu_preflop_abstract_nashconv)

add_executable(gtosd_hu_preflop_tre_validation hu_preflop_tre_validation.cpp)
target_link_libraries(gtosd_hu_preflop_tre_validation
  PRIVATE
    gtosd::preflop_certifier
    gtosd::solver_validation
    nlohmann_json::nlohmann_json)
gtosd_set_warnings(gtosd_hu_preflop_tre_validation)
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

if(GTOSD_BUILD_PREFLOP_BLUEPRINT)
  add_executable(gtosd_preflop_blueprint_catalog preflop_blueprint_catalog.cpp)
  target_link_libraries(gtosd_preflop_blueprint_catalog PRIVATE gtosd::card_abstraction)
  gtosd_set_warnings(gtosd_preflop_blueprint_catalog)
  add_test(NAME gtosd_preflop_blueprint_catalog COMMAND gtosd_preflop_blueprint_catalog)
  set_tests_properties(gtosd_preflop_blueprint_catalog
    PROPERTIES LABELS "preflop_blueprint;p1;catalog")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_catalog PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
endif()

if(GTOSD_BUILD_PREFLOP_BLUEPRINT)
  add_executable(gtosd_preflop_blueprint_resources preflop_blueprint_resources.cpp)
  target_link_libraries(gtosd_preflop_blueprint_resources PRIVATE gtosd::card_abstraction)
  gtosd_set_warnings(gtosd_preflop_blueprint_resources)
  add_test(NAME gtosd_preflop_blueprint_resources
    COMMAND gtosd_preflop_blueprint_resources --threads 8 --verify-oracle 20000)
  set_tests_properties(gtosd_preflop_blueprint_resources
    PROPERTIES LABELS "preflop_blueprint;p2;resources")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_resources PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
endif()

if(GTOSD_BUILD_PREFLOP_BLUEPRINT)
  add_executable(gtosd_preflop_blueprint_buckets preflop_blueprint_buckets.cpp)
  target_link_libraries(gtosd_preflop_blueprint_buckets PRIVATE gtosd::card_abstraction)
  gtosd_set_warnings(gtosd_preflop_blueprint_buckets)
  add_test(NAME gtosd_preflop_blueprint_buckets_smoke
    COMMAND gtosd_preflop_blueprint_buckets --synthetic-groups --threads 8
      --flop 8 --turn 16 --river 32 --restarts 2 --screening-iterations 3
      --max-iterations 4 --screening-sample 50000)
  set_tests_properties(gtosd_preflop_blueprint_buckets_smoke
    PROPERTIES LABELS "preflop_blueprint;p3;buckets")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_buckets_smoke PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
endif()

if(GTOSD_BUILD_PREFLOP_BLUEPRINT)
  add_executable(gtosd_preflop_blueprint_game preflop_blueprint_game.cpp)
  target_link_libraries(gtosd_preflop_blueprint_game PRIVATE gtosd::preflop_blueprint)
  gtosd_set_warnings(gtosd_preflop_blueprint_game)
  add_test(NAME gtosd_preflop_blueprint_game_report
    COMMAND gtosd_preflop_blueprint_game
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_co40_v1.json)
  set_tests_properties(gtosd_preflop_blueprint_game_report
    PROPERTIES LABELS "preflop_blueprint;p4;game")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_game_report PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
endif()

if(GTOSD_BUILD_PREFLOP_BLUEPRINT)
  add_executable(gtosd_preflop_blueprint_traversal preflop_blueprint_traversal.cpp)
  target_link_libraries(gtosd_preflop_blueprint_traversal
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction)
  gtosd_set_warnings(gtosd_preflop_blueprint_traversal)
  add_test(NAME gtosd_preflop_blueprint_traversal_report
    COMMAND gtosd_preflop_blueprint_traversal
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_co40_v1.json
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --boards 10)
  set_tests_properties(gtosd_preflop_blueprint_traversal_report
    PROPERTIES LABELS "preflop_blueprint;p5;kernels")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_traversal_report PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
endif()

if(GTOSD_BUILD_PREFLOP_BLUEPRINT)
  add_executable(gtosd_preflop_blueprint_train preflop_blueprint_train.cpp)
  target_link_libraries(gtosd_preflop_blueprint_train
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction)
  gtosd_set_warnings(gtosd_preflop_blueprint_train)
  add_test(NAME gtosd_preflop_blueprint_train_smoke
    COMMAND gtosd_preflop_blueprint_train
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --iterations 20 --batch 8 --threads 4 --eval-flops 2 --eval-every 10
      --policy-out ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_train_smoke_policy.bin)
  set_tests_properties(gtosd_preflop_blueprint_train_smoke
    PROPERTIES LABELS "preflop_blueprint;p6;trainer"
               FIXTURES_SETUP preflop_blueprint_smoke_policy)
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_train_smoke PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()

  add_executable(gtosd_preflop_blueprint_certify preflop_blueprint_certify.cpp)
  target_link_libraries(gtosd_preflop_blueprint_certify
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction)
  gtosd_set_warnings(gtosd_preflop_blueprint_certify)
  add_test(NAME gtosd_preflop_blueprint_certify_smoke
    COMMAND gtosd_preflop_blueprint_certify
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --uniform --threads 4 --flop-limit 4 --chunk 2
      --output ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_certify_smoke.json)
  set_tests_properties(gtosd_preflop_blueprint_certify_smoke
    PROPERTIES LABELS "preflop_blueprint;p7;certifier")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_certify_smoke PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()

  add_executable(gtosd_preflop_blueprint_export preflop_blueprint_export.cpp)
  target_link_libraries(gtosd_preflop_blueprint_export
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction gtosd::core nlohmann_json::nlohmann_json)
  gtosd_set_warnings(gtosd_preflop_blueprint_export)
  add_test(NAME gtosd_preflop_blueprint_export_smoke
    COMMAND gtosd_preflop_blueprint_export
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --policy ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_train_smoke_policy.bin
      --eval-flops 2 --threads 4 --iterations 20
      --output ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_export_smoke_chart.json)
  set_tests_properties(gtosd_preflop_blueprint_export_smoke
    PROPERTIES LABELS "preflop_blueprint;p8;export"
               FIXTURES_REQUIRED preflop_blueprint_smoke_policy
               FIXTURES_SETUP preflop_blueprint_smoke_chart)
  add_test(NAME gtosd_preflop_blueprint_query_smoke
    COMMAND gtosd_preflop_blueprint_export
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --policy ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_train_smoke_policy.bin
      --query-history raise_3,call --query-hand AhKs --query-board 7s8d9c)
  set_tests_properties(gtosd_preflop_blueprint_query_smoke
    PROPERTIES LABELS "preflop_blueprint;p8;export"
               FIXTURES_REQUIRED preflop_blueprint_smoke_policy)

  add_test(NAME gtosd_preflop_blueprint_postflop_tree_smoke
    COMMAND gtosd_preflop_blueprint_export
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --policy ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_train_smoke_policy.bin
      --postflop-tree ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_postflop_tree_smoke.json)
  set_tests_properties(gtosd_preflop_blueprint_postflop_tree_smoke
    PROPERTIES LABELS "preflop_blueprint;p8;export"
               FIXTURES_REQUIRED preflop_blueprint_smoke_policy)

  add_executable(gtosd_preflop_blueprint_compare preflop_blueprint_compare.cpp)
  target_link_libraries(gtosd_preflop_blueprint_compare PRIVATE gtosd::preflop_blueprint gtosd::core)
  gtosd_set_warnings(gtosd_preflop_blueprint_compare)
  add_test(NAME gtosd_preflop_blueprint_compare_smoke
    COMMAND gtosd_preflop_blueprint_compare
      --candidate ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_export_smoke_chart.json
      --baseline ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_export_smoke_chart.json
      --reference ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/hu_preflop_co40_reference_v1.json
      --max-gain 100
      --output ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_compare_smoke.json)
  set_tests_properties(gtosd_preflop_blueprint_compare_smoke
    PROPERTIES LABELS "preflop_blueprint;p8;comparator"
               FIXTURES_REQUIRED preflop_blueprint_smoke_chart)
  foreach(smoke gtosd_preflop_blueprint_export_smoke gtosd_preflop_blueprint_query_smoke
                gtosd_preflop_blueprint_compare_smoke gtosd_preflop_blueprint_postflop_tree_smoke)
    if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
      set_tests_properties(${smoke} PROPERTIES
        ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
    endif()
  endforeach()
endif()
