find_package(benchmark CONFIG REQUIRED)

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
