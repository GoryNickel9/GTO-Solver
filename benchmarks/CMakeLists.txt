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
