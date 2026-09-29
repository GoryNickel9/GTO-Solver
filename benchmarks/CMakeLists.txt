find_package(benchmark CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(gtosd_constrained_br_audit constrained_br_audit.cpp)
target_link_libraries(gtosd_constrained_br_audit PRIVATE gtosd::best_response nlohmann_json::nlohmann_json)
gtosd_set_warnings(gtosd_constrained_br_audit)

if(GTOSD_BUILD_PREFLOP_BLUEPRINT)
  add_executable(gtosd_preflop_blueprint_checkpoint_policies preflop_blueprint_checkpoint_policies.cpp)
  target_link_libraries(gtosd_preflop_blueprint_checkpoint_policies PRIVATE
    gtosd::preflop_blueprint nlohmann_json::nlohmann_json)
  gtosd_set_warnings(gtosd_preflop_blueprint_checkpoint_policies)
  add_executable(gtosd_preflop_blueprint_history_census preflop_blueprint_history_census.cpp)
  target_link_libraries(gtosd_preflop_blueprint_history_census PRIVATE
    gtosd::preflop_blueprint nlohmann_json::nlohmann_json)
  target_compile_definitions(gtosd_preflop_blueprint_history_census PRIVATE
    GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  gtosd_set_warnings(gtosd_preflop_blueprint_history_census)
  add_executable(gtosd_short_deck_constrained_audit short_deck_constrained_audit.cpp)
  target_link_libraries(gtosd_short_deck_constrained_audit PRIVATE
    gtosd::preflop_blueprint gtosd::best_response nlohmann_json::nlohmann_json)
  target_compile_definitions(gtosd_short_deck_constrained_audit PRIVATE
    GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  gtosd_set_warnings(gtosd_short_deck_constrained_audit)
endif()

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
  add_executable(gtosd_preflop_blueprint_flop_clustering_audit
    preflop_blueprint_flop_clustering_audit.cpp)
  target_link_libraries(gtosd_preflop_blueprint_flop_clustering_audit
    PRIVATE gtosd::card_abstraction nlohmann_json::nlohmann_json)
  gtosd_set_warnings(gtosd_preflop_blueprint_flop_clustering_audit)
  add_executable(gtosd_preflop_blueprint_river_clustering_audit
    preflop_blueprint_river_clustering_audit.cpp)
  target_link_libraries(gtosd_preflop_blueprint_river_clustering_audit
    PRIVATE gtosd::card_abstraction nlohmann_json::nlohmann_json)
  gtosd_set_warnings(gtosd_preflop_blueprint_river_clustering_audit)
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
  add_executable(gtosd_preflop_blueprint_checkdown preflop_blueprint_checkdown.cpp)
  target_link_libraries(gtosd_preflop_blueprint_checkdown PRIVATE gtosd::preflop_blueprint)
  gtosd_set_warnings(gtosd_preflop_blueprint_checkdown)
  add_executable(gtosd_preflop_blueprint_monker_charts preflop_blueprint_monker_charts.cpp)
  target_link_libraries(gtosd_preflop_blueprint_monker_charts PRIVATE gtosd::preflop_blueprint)
  gtosd_set_warnings(gtosd_preflop_blueprint_monker_charts)
  add_executable(gtosd_preflop_blueprint_monker_buckets preflop_blueprint_monker_buckets.cpp)
  target_link_libraries(gtosd_preflop_blueprint_monker_buckets PRIVATE gtosd::preflop_blueprint)
  gtosd_set_warnings(gtosd_preflop_blueprint_monker_buckets)
  add_executable(gtosd_preflop_blueprint_monker_values preflop_blueprint_monker_values.cpp)
  target_link_libraries(gtosd_preflop_blueprint_monker_values
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction nlohmann_json::nlohmann_json)
  gtosd_set_warnings(gtosd_preflop_blueprint_monker_values)
  add_test(NAME gtosd_preflop_blueprint_game_report
    COMMAND gtosd_preflop_blueprint_game
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_co40_v1.json)
  set_tests_properties(gtosd_preflop_blueprint_game_report
    PROPERTIES LABELS "preflop_blueprint;p4;game")
  if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
    set_tests_properties(gtosd_preflop_blueprint_game_report PROPERTIES
      ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
  endif()
  # The preflop trees of the MonkerSolver chart sets against their manifests
  # (benchmarks/monker/*_tree_manifest.tsv, written from the chart folders by
  # --write-manifest): the 3-way 50a and 100a trees in pot mode and the HU50
  # tree, whose names the multiway fold rule of chart_nodes must not change.
  # The last run checks that a wrong tree fails.
  add_executable(gtosd_preflop_blueprint_monker_tree preflop_blueprint_monker_tree.cpp)
  target_link_libraries(gtosd_preflop_blueprint_monker_tree
    PRIVATE gtosd::preflop_blueprint nlohmann_json::nlohmann_json)
  gtosd_set_warnings(gtosd_preflop_blueprint_monker_tree)
  foreach(tree IN ITEMS "3WAY50;3WAY50_donk;54" "3WAY100;3WAY100_donk;54" "HU50;HU50;8")
    list(GET tree 0 manifest)
    list(GET tree 1 tree_config)
    list(GET tree 2 tree_nodes)
    string(TOLOWER ${manifest} tree_name)
    add_test(NAME gtosd_preflop_blueprint_monker_tree_${tree_name}
      COMMAND gtosd_preflop_blueprint_monker_tree
        --config ${PROJECT_SOURCE_DIR}/benchmarks/monker/${tree_config}.json
        --manifest ${PROJECT_SOURCE_DIR}/benchmarks/monker/${manifest}_tree_manifest.tsv
        --json ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_monker_tree_${tree_name}.json)
    set_tests_properties(gtosd_preflop_blueprint_monker_tree_${tree_name} PROPERTIES
      LABELS "preflop_blueprint;research;monker"
      PASS_REGULAR_EXPRESSION
        "PREFLOP_BLUEPRINT_MONKER_TREE=PASS nodes=${tree_nodes} files=${tree_nodes}")
  endforeach()
  add_test(NAME gtosd_preflop_blueprint_monker_tree_mismatch
    COMMAND gtosd_preflop_blueprint_monker_tree
      --config ${PROJECT_SOURCE_DIR}/benchmarks/monker/HU50.json
      --manifest ${PROJECT_SOURCE_DIR}/benchmarks/monker/3WAY50_tree_manifest.tsv)
  set_tests_properties(gtosd_preflop_blueprint_monker_tree_mismatch PROPERTIES
    LABELS "preflop_blueprint;research;monker"
    PASS_REGULAR_EXPRESSION "PREFLOP_BLUEPRINT_MONKER_TREE=FAIL missing=[1-9]")
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
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction nlohmann_json::nlohmann_json)
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
  # Policy snapshots with a stride: charts every 2 iterations, a policy in the
  # snapshots of iterations 4 and 8 only. The second run asks for an impossible
  # free-space reserve: both policies are skipped and the training still ends.
  add_test(NAME gtosd_preflop_blueprint_train_policy_snapshot_smoke
    COMMAND gtosd_preflop_blueprint_train
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --iterations 8 --batch 8 --threads 2 --eval-every 0
      --chart-every 2 --chart-dir ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_policy_snapshot_smoke
      --policy-snapshots --policy-snapshot-every 4 --policy-snapshot-reserve-gb 0)
  set_tests_properties(gtosd_preflop_blueprint_train_policy_snapshot_smoke PROPERTIES
    LABELS "preflop_blueprint;research;monker"
    PASS_REGULAR_EXPRESSION
      "\"iteration\":4,\"training_seconds\":[^,}]*,\"policy_fingerprint\":\"fnv1a64:[0-9a-f]+\".*\"iteration\":8,\"training_seconds\":[^,}]*,\"policy_fingerprint\":\"fnv1a64:[0-9a-f]+\".*PREFLOP_BLUEPRINT_TRAIN=ITERATION_LIMIT"
    FAIL_REGULAR_EXPRESSION
      "\"iteration\":[26],\"training_seconds\":[^,}]*,\"policy|policy_snapshot_(skipped|failed)|PREFLOP_BLUEPRINT_TRAIN=FAIL")
  add_test(NAME gtosd_preflop_blueprint_train_policy_snapshot_disk_smoke
    COMMAND gtosd_preflop_blueprint_train
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --iterations 8 --batch 8 --threads 2 --eval-every 0
      --chart-every 2
      --chart-dir ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_policy_snapshot_disk_smoke
      --policy-snapshots --policy-snapshot-every 4 --policy-snapshot-reserve-gb 1000000000)
  set_tests_properties(gtosd_preflop_blueprint_train_policy_snapshot_disk_smoke PROPERTIES
    LABELS "preflop_blueprint;research;monker"
    PASS_REGULAR_EXPRESSION
      "\"event\":\"policy_snapshot_skipped\",\"iteration\":4,\"reason\":\"disk_space\".*\"event\":\"policy_snapshot_skipped\",\"iteration\":8,\"reason\":\"disk_space\".*PREFLOP_BLUEPRINT_TRAIN=ITERATION_LIMIT"
    FAIL_REGULAR_EXPRESSION
      "\"policy_seconds\"|\"iteration\":[26],\"reason\"|policy_snapshot_failed|PREFLOP_BLUEPRINT_TRAIN=FAIL")
  add_test(NAME gtosd_preflop_blueprint_train_policy_snapshot_stride_rejected
    COMMAND gtosd_preflop_blueprint_train
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --iterations 1 --chart-every 2
      --chart-dir ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_policy_snapshot_rejected
      --policy-snapshots --policy-snapshot-every 3)
  set_tests_properties(gtosd_preflop_blueprint_train_policy_snapshot_stride_rejected PROPERTIES
    LABELS "preflop_blueprint;research;monker"
    PASS_REGULAR_EXPRESSION
      "PREFLOP_BLUEPRINT_TRAIN=FAIL --policy-snapshot-every requires --policy-snapshots and a multiple of --chart-every")
  # Preflop lock: the CO root of the reduced game plays the rows of the snapshot
  # charts of iteration 8 while everything else trains; an unknown chart is refused.
  set_tests_properties(gtosd_preflop_blueprint_train_policy_snapshot_smoke PROPERTIES
    FIXTURES_SETUP preflop_blueprint_policy_snapshot_charts)
  add_test(NAME gtosd_preflop_blueprint_train_lock_smoke
    COMMAND gtosd_preflop_blueprint_train
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --iterations 4 --batch 8 --threads 2 --eval-every 0
      --scheme dcfr --update alternating --lazy-discount
      --chart-every 2 --chart-dir ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_lock_smoke
      --lock-charts ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_policy_snapshot_smoke/it_8
      --lock-nodes CO/CO_strategy.txt)
  set_tests_properties(gtosd_preflop_blueprint_train_lock_smoke PROPERTIES
    LABELS "preflop_blueprint;research;monker"
    FIXTURES_REQUIRED preflop_blueprint_policy_snapshot_charts
    PASS_REGULAR_EXPRESSION
      "\"preflop_lock\": .\"charts\":\"[^\"]*\",\"files\":.\"CO/CO_strategy.txt\".,\"fingerprint\":\"[0-9a-f]+\",\"outside_range_rows\":0,\"rows\":81.*PREFLOP_BLUEPRINT_TRAIN=ITERATION_LIMIT"
    FAIL_REGULAR_EXPRESSION "PREFLOP_BLUEPRINT_TRAIN=FAIL")
  add_test(NAME gtosd_preflop_blueprint_train_lock_rejected
    COMMAND gtosd_preflop_blueprint_train
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --iterations 1 --eval-every 0
      --lock-charts ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_policy_snapshot_smoke/it_8
      --lock-nodes CO/XX_strategy.txt)
  set_tests_properties(gtosd_preflop_blueprint_train_lock_rejected PROPERTIES
    LABELS "preflop_blueprint;research;monker"
    PASS_REGULAR_EXPRESSION "PREFLOP_BLUEPRINT_TRAIN=FAIL unknown chart CO/XX_strategy.txt")
  foreach(smoke gtosd_preflop_blueprint_train_policy_snapshot_smoke
                gtosd_preflop_blueprint_train_policy_snapshot_disk_smoke
                gtosd_preflop_blueprint_train_policy_snapshot_stride_rejected
                gtosd_preflop_blueprint_train_lock_smoke
                gtosd_preflop_blueprint_train_lock_rejected)
    if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
      set_tests_properties(${smoke} PROPERTIES
        ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
    endif()
  endforeach()

  add_executable(gtosd_preflop_blueprint_certify preflop_blueprint_certify.cpp)
  add_executable(gtosd_preflop_blueprint_abstract_br preflop_blueprint_abstract_br.cpp)
  target_link_libraries(gtosd_preflop_blueprint_abstract_br
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction nlohmann_json::nlohmann_json)
  gtosd_set_warnings(gtosd_preflop_blueprint_abstract_br)
  add_executable(gtosd_preflop_blueprint_history_rows preflop_blueprint_history_rows.cpp)
  target_link_libraries(gtosd_preflop_blueprint_history_rows
    PRIVATE gtosd::preflop_blueprint nlohmann_json::nlohmann_json)
  target_compile_definitions(gtosd_preflop_blueprint_history_rows PRIVATE
    GTOSD_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  gtosd_set_warnings(gtosd_preflop_blueprint_history_rows)
  add_executable(gtosd_preflop_blueprint_bucket_diagnostics
    preflop_blueprint_bucket_diagnostics.cpp)
  add_executable(gtosd_preflop_blueprint_temporal_witness preflop_blueprint_temporal_witness.cpp)
  target_link_libraries(gtosd_preflop_blueprint_temporal_witness
    PRIVATE gtosd::card_abstraction nlohmann_json::nlohmann_json)
  gtosd_set_warnings(gtosd_preflop_blueprint_temporal_witness)
  target_link_libraries(gtosd_preflop_blueprint_bucket_diagnostics
    PRIVATE gtosd::preflop_blueprint gtosd::card_abstraction nlohmann_json::nlohmann_json)
  gtosd_set_warnings(gtosd_preflop_blueprint_bucket_diagnostics)
  target_link_libraries(gtosd_preflop_blueprint_certify PRIVATE nlohmann_json::nlohmann_json)
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
      --query-history raise_5,call --query-hand AhKs --query-board 7s8d9c)
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
  # MonkerSolver-format charts of the smoke policy, then that chart set played
  # inside the game (its loss is the three-decimal rounding of the charts):
  # two sampled flops, then a partial canonical pass with suit orbits.
  add_test(NAME gtosd_preflop_blueprint_monker_charts_smoke
    COMMAND gtosd_preflop_blueprint_monker_charts
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json
      --policy ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_train_smoke_policy.bin
      --output-dir ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_monker_charts_smoke)
  set_tests_properties(gtosd_preflop_blueprint_monker_charts_smoke
    PROPERTIES LABELS "preflop_blueprint;research;monker"
               FIXTURES_REQUIRED preflop_blueprint_smoke_policy
               FIXTURES_SETUP preflop_blueprint_smoke_monker_charts)
  add_test(NAME gtosd_preflop_blueprint_monker_values_smoke
    COMMAND gtosd_preflop_blueprint_monker_values
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --policy ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_train_smoke_policy.bin
      --charts self=${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_monker_charts_smoke
      --flops 2 --threads 2
      --out ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_monker_values_smoke.json)
  set_tests_properties(gtosd_preflop_blueprint_monker_values_smoke
    PROPERTIES LABELS "preflop_blueprint;research;monker"
               FIXTURES_REQUIRED "preflop_blueprint_smoke_policy;preflop_blueprint_smoke_monker_charts"
               FIXTURES_SETUP preflop_blueprint_smoke_monker_values)
  add_test(NAME gtosd_preflop_blueprint_monker_values_orbit_smoke
    COMMAND gtosd_preflop_blueprint_monker_values
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --policy ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_train_smoke_policy.bin
      --charts self=${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_monker_charts_smoke
      --all-flops --flop-limit 2 --threads 2 --no-combo-values)
  set_tests_properties(gtosd_preflop_blueprint_monker_values_orbit_smoke
    PROPERTIES LABELS "preflop_blueprint;research;monker"
               FIXTURES_REQUIRED "preflop_blueprint_smoke_policy;preflop_blueprint_smoke_monker_charts")
  # The best response against the same chart set (--exploit): both chart
  # players, with a river-restricted pass, on a partial canonical pass. The
  # summary goes to a directory removed beforehand: the tool creates it.
  set(exploit_smoke_dir ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_monker_values_exploit_smoke)
  add_test(NAME gtosd_preflop_blueprint_monker_values_exploit_smoke_clean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${exploit_smoke_dir})
  set_tests_properties(gtosd_preflop_blueprint_monker_values_exploit_smoke_clean
    PROPERTIES LABELS "preflop_blueprint;research;monker"
               FIXTURES_SETUP preflop_blueprint_monker_values_exploit_dir)
  add_test(NAME gtosd_preflop_blueprint_monker_values_exploit_smoke
    COMMAND gtosd_preflop_blueprint_monker_values
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --policy ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_train_smoke_policy.bin
      --charts self=${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_monker_charts_smoke
      --all-flops --flop-limit 2 --threads 2 --no-combo-values
      --exploit self --exploit-streets river
      --exploit-out ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_monker_values_exploit_smoke.json
      --exploit-summary ${exploit_smoke_dir}/summary/exploit.txt)
  set_tests_properties(gtosd_preflop_blueprint_monker_values_exploit_smoke
    PROPERTIES LABELS "preflop_blueprint;research;monker"
               FIXTURES_REQUIRED
                 "preflop_blueprint_smoke_policy;preflop_blueprint_smoke_monker_charts;preflop_blueprint_monker_values_exploit_dir")
  # An output path that cannot be written fails before the first pass.
  add_test(NAME gtosd_preflop_blueprint_monker_values_exploit_out_refused
    COMMAND gtosd_preflop_blueprint_monker_values
      --config ${PROJECT_SOURCE_DIR}/benchmarks/fixtures/preflop_blueprint_hu10_reduced_v1.json
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --buckets-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_buckets_200_500_1000
      --policy ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_train_smoke_policy.bin
      --charts self=${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_monker_charts_smoke
      --all-flops --flop-limit 2 --threads 2 --no-combo-values
      --exploit self --exploit-out ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(gtosd_preflop_blueprint_monker_values_exploit_out_refused
    PROPERTIES LABELS "preflop_blueprint;research;monker"
               PASS_REGULAR_EXPRESSION
                 "PREFLOP_BLUEPRINT_MONKER_VALUES=FAIL --exploit-out names a directory"
               FAIL_REGULAR_EXPRESSION "stage one|flops_done")
  # Per-board bucket tables with an exact flop (one id per suit orbit), the
  # 15x4 turn and the 15-level river, checked by
  # gtosd_preflop_blueprint_monker_buckets_tests.
  add_test(NAME gtosd_preflop_blueprint_monker_buckets_exact_smoke
    COMMAND gtosd_preflop_blueprint_monker_buckets
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --output-dir ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_monker_buckets_exact
      --flop-exact --levels 15 --tiers 4 --threads 4)
  set_tests_properties(gtosd_preflop_blueprint_monker_buckets_exact_smoke
    PROPERTIES LABELS "preflop_blueprint;research;monker"
               FIXTURES_SETUP preflop_blueprint_monker_buckets_exact)
  # The default path without per-street flags (must keep the 15x4 tables and
  # report of the builder before per-street settings) and a per-street run
  # (flop override equal to --levels/--tiers, turn and river at 30 levels, turn
  # tiers from --tiers: the 30x4 turn and 30-level river tables), both checked by
  # fingerprint in gtosd_preflop_blueprint_monker_buckets_*_tests.
  add_test(NAME gtosd_preflop_blueprint_monker_buckets_default_smoke
    COMMAND gtosd_preflop_blueprint_monker_buckets
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --output-dir ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_monker_buckets_default
      --levels 15 --tiers 4 --threads 4)
  set_tests_properties(gtosd_preflop_blueprint_monker_buckets_default_smoke
    PROPERTIES LABELS "preflop_blueprint;research;monker"
               FIXTURES_SETUP preflop_blueprint_monker_buckets_default)
  add_test(NAME gtosd_preflop_blueprint_monker_buckets_streets_smoke
    COMMAND gtosd_preflop_blueprint_monker_buckets
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --output-dir ${CMAKE_CURRENT_BINARY_DIR}/preflop_blueprint_monker_buckets_streets
      --levels 15 --tiers 4 --flop-levels 15 --flop-tiers 4 --turn-levels 30
      --river-levels 30 --threads 4)
  set_tests_properties(gtosd_preflop_blueprint_monker_buckets_streets_smoke
    PROPERTIES LABELS "preflop_blueprint;research;monker"
               FIXTURES_SETUP preflop_blueprint_monker_buckets_streets)
  add_test(NAME gtosd_preflop_blueprint_monker_buckets_exact_levels_refused
    COMMAND gtosd_preflop_blueprint_monker_buckets
      --resources-dir ${PROJECT_SOURCE_DIR}/out/preflop_blueprint_resources
      --flop-exact --flop-levels 10)
  set_tests_properties(gtosd_preflop_blueprint_monker_buckets_exact_levels_refused
    PROPERTIES LABELS "preflop_blueprint;research;monker"
               PASS_REGULAR_EXPRESSION
                 "PREFLOP_BLUEPRINT_MONKER_BUCKETS=FAIL --flop-exact excludes --flop-levels and --flop-tiers")
  foreach(smoke gtosd_preflop_blueprint_export_smoke gtosd_preflop_blueprint_query_smoke
                gtosd_preflop_blueprint_compare_smoke gtosd_preflop_blueprint_postflop_tree_smoke
                gtosd_preflop_blueprint_monker_charts_smoke gtosd_preflop_blueprint_monker_values_smoke
                gtosd_preflop_blueprint_monker_values_orbit_smoke
                gtosd_preflop_blueprint_monker_values_exploit_smoke
                gtosd_preflop_blueprint_monker_values_exploit_out_refused
                gtosd_preflop_blueprint_monker_buckets_exact_smoke
                gtosd_preflop_blueprint_monker_buckets_default_smoke
                gtosd_preflop_blueprint_monker_buckets_streets_smoke
                gtosd_preflop_blueprint_monker_buckets_exact_levels_refused)
    if(GTOSD_TEST_RUNTIME_ENVIRONMENT)
      set_tests_properties(${smoke} PROPERTIES
        ENVIRONMENT_MODIFICATION "${GTOSD_TEST_RUNTIME_ENVIRONMENT}")
    endif()
  endforeach()
endif()
