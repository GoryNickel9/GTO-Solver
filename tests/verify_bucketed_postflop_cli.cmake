if(NOT DEFINED GTOSD_CLI OR NOT DEFINED GTOSD_FIXTURE OR NOT DEFINED GTOSD_OUTPUT_DIR)
  message(FATAL_ERROR "GTOSD_CLI, GTOSD_FIXTURE and GTOSD_OUTPUT_DIR are required")
endif()

set(checkpoint "${GTOSD_OUTPUT_DIR}/bucketed_postflop_cli.chk")
set(report_prefix "${GTOSD_OUTPUT_DIR}/bucketed_postflop_cli_report")
set(feature_cache "${GTOSD_OUTPUT_DIR}/bucketed_postflop_cli.features")
file(REMOVE "${checkpoint}" "${checkpoint}.tmp" "${report_prefix}.json" "${report_prefix}.md"
            "${feature_cache}" "${feature_cache}.tmp")

execute_process(
  COMMAND "${GTOSD_CLI}" postflop build-feature-cache "${GTOSD_FIXTURE}" "${feature_cache}"
  RESULT_VARIABLE cache_result
  OUTPUT_VARIABLE cache_output
  ERROR_VARIABLE cache_error)
if(NOT cache_result EQUAL 0 OR
   NOT cache_output MATCHES "cache_fingerprint=[0-9a-f]+" OR
   NOT EXISTS "${feature_cache}")
  message(FATAL_ERROR
    "feature cache build failed (${cache_result})\n${cache_output}\n${cache_error}")
endif()

execute_process(
  COMMAND "${GTOSD_CLI}" postflop solve-bucketed "${GTOSD_FIXTURE}" 100
          "${checkpoint}" "${report_prefix}" 16 16 8 100 "${feature_cache}"
  RESULT_VARIABLE solve_result
  OUTPUT_VARIABLE solve_output
  ERROR_VARIABLE solve_error)
if(NOT solve_result EQUAL 0)
  message(FATAL_ERROR "bucketed solve failed (${solve_result})\n${solve_output}\n${solve_error}")
endif()

execute_process(
  COMMAND "${GTOSD_CLI}" postflop resume-bucketed "${GTOSD_FIXTURE}" 200
          "${checkpoint}" "${report_prefix}" 16 16 8 200 "${feature_cache}"
  RESULT_VARIABLE resume_result
  OUTPUT_VARIABLE resume_output
  ERROR_VARIABLE resume_error)
if(NOT resume_result EQUAL 0)
  message(FATAL_ERROR
    "bucketed resume failed (${resume_result})\n${resume_output}\n${resume_error}")
endif()

file(READ "${report_prefix}.json" report)
string(JSON uses_bucketing GET "${report}" uses_bucketing)
string(JSON exact_outcomes GET "${report}" exact_outcomes)
string(JSON abstraction_kind GET "${report}" abstraction_kind)
string(JSON abstraction_fingerprint GET "${report}" abstraction_fingerprint)
string(JSON buckets GET "${report}" buckets_per_partition)
string(JSON compression GET "${report}" abstraction_compression_ratio)
string(JSON nash_conv GET "${report}" normalized_nash_conv)
string(JSON iterations GET "${report}" iterations)
string(JSON cache_reused GET "${report}" feature_cache_reused)
string(JSON cache_fingerprint GET "${report}" feature_cache_fingerprint)
if(NOT uses_bucketing OR NOT exact_outcomes OR
   NOT abstraction_kind STREQUAL "equity_feature_kmeans" OR
   abstraction_fingerprint STREQUAL "" OR NOT buckets EQUAL 8 OR
   NOT compression GREATER 1.0 OR NOT nash_conv LESS 0.01 OR NOT iterations EQUAL 200 OR
   NOT cache_reused OR cache_fingerprint STREQUAL "")
  message(FATAL_ERROR "bucketed report contract mismatch: ${report}")
endif()

execute_process(
  COMMAND "${GTOSD_CLI}" postflop query-bucketed "${GTOSD_FIXTURE}" "${checkpoint}" 0 0 8
          "${feature_cache}"
  RESULT_VARIABLE query_result
  OUTPUT_VARIABLE query_output
  ERROR_VARIABLE query_error)
if(NOT query_result EQUAL 0 OR
   NOT query_output MATCHES "bucketing=true abstraction_bucket=[0-9]+ abstraction_bucket_size=[0-9]+")
  message(FATAL_ERROR "bucketed query failed (${query_result})\n${query_output}\n${query_error}")
endif()

execute_process(
  COMMAND "${GTOSD_CLI}" postflop certify-bucketed "${GTOSD_FIXTURE}" "${checkpoint}" 8
          "${feature_cache}"
  RESULT_VARIABLE certify_result
  OUTPUT_VARIABLE certify_output
  ERROR_VARIABLE certify_error)
if(NOT certify_result EQUAL 0 OR NOT certify_output MATCHES "gate_below_one_percent=pass")
  message(FATAL_ERROR
    "bucketed exact certification failed (${certify_result})\n${certify_output}\n${certify_error}")
endif()

execute_process(
  COMMAND "${GTOSD_CLI}" postflop certify "${GTOSD_FIXTURE}" "${checkpoint}"
  RESULT_VARIABLE exact_result
  OUTPUT_QUIET
  ERROR_QUIET)
execute_process(
  COMMAND "${GTOSD_CLI}" postflop certify-bucketed "${GTOSD_FIXTURE}" "${checkpoint}" 7
          "${feature_cache}"
  RESULT_VARIABLE wrong_bucket_result
  OUTPUT_QUIET
  ERROR_QUIET)
if(exact_result EQUAL 0 OR wrong_bucket_result EQUAL 0)
  message(FATAL_ERROR "checkpoint identity did not reject exact or wrong-bucket certification")
endif()

file(REMOVE "${checkpoint}" "${report_prefix}.json" "${report_prefix}.md" "${feature_cache}")
