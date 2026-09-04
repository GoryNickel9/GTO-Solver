if(NOT DEFINED GTOSD_CLI OR NOT DEFINED GTOSD_TST_FIXTURE OR
   NOT DEFINED GTOSD_AHK_FIXTURE OR NOT DEFINED GTOSD_OUTPUT_DIR)
  message(FATAL_ERROR
    "GTOSD_CLI, GTOSD_TST_FIXTURE, GTOSD_AHK_FIXTURE and GTOSD_OUTPUT_DIR are required")
endif()

set(tst_report "${GTOSD_OUTPUT_DIR}/tst_bucketed_preflight.json")
set(turn_report "${GTOSD_OUTPUT_DIR}/turn_bucketed_preflight.json")
set(turn_cache "${GTOSD_OUTPUT_DIR}/turn_bucketed_preflight.features")
set(turn_qualification "${GTOSD_OUTPUT_DIR}/turn_bucketed_qualification.json")
file(REMOVE "${tst_report}" "${turn_report}" "${turn_cache}" "${turn_cache}.tmp"
            "${turn_qualification}")

execute_process(
  COMMAND "${GTOSD_CLI}" postflop preflight-bucketing-gto-plus
          "${GTOSD_TST_FIXTURE}" "${tst_report}" 16,32,64 2000000000 10737418240
  RESULT_VARIABLE tst_result
  OUTPUT_VARIABLE tst_output
  ERROR_VARIABLE tst_error)
if(NOT tst_result EQUAL 0)
  message(FATAL_ERROR "TST bucketing preflight failed (${tst_result})\n${tst_output}\n${tst_error}")
endif()

file(READ "${tst_report}" tst)
string(JSON schema GET "${tst}" schema)
string(JSON status GET "${tst}" selection_status)
string(JSON exact_outcomes GET "${tst}" exact_outcomes)
string(JSON uses_bucketing GET "${tst}" uses_bucketing)
string(JSON format_limit_ok GET "${tst}" feature_cache format_limit_ok)
string(JSON cache_workers GET "${tst}" feature_cache worker_count)
string(JSON cache_atomic_bytes GET "${tst}" feature_cache atomic_write_bytes_upper_bound)
string(JSON preflight_solver_threads GET "${tst}" memory_model solver_threads)
string(JSON k16 GET "${tst}" candidates 0 buckets_per_partition)
string(JSON k16_fits GET "${tst}" candidates 0 in_ram_meets_requested_budget)
string(JSON k32 GET "${tst}" candidates 1 buckets_per_partition)
string(JSON k32_fits GET "${tst}" candidates 1 in_ram_meets_requested_budget)
string(JSON k64 GET "${tst}" candidates 2 buckets_per_partition)
string(JSON k64_fits GET "${tst}" candidates 2 in_ram_meets_requested_budget)
string(JSON exact_actions GET "${tst}" exact_layout action_entries)
string(JSON abstract_actions GET "${tst}" candidates 1 abstract_action_entries_upper_bound)
string(JSON k32_state_bytes GET "${tst}" candidates 1 out_of_core_backing_store_bytes)
string(JSON k32_disk_bytes GET "${tst}" candidates 1 estimated_disk_bytes_with_cache)
math(EXPR k32_required_disk_bytes "${k32_state_bytes} + ${cache_atomic_bytes}")
if(NOT schema STREQUAL "gtosd.card_abstraction_preflight.v1" OR
   NOT status STREQUAL "FEASIBILITY_ONLY_NOT_PROMOTABLE" OR
   NOT exact_outcomes OR NOT uses_bucketing OR NOT format_limit_ok OR
   NOT cache_workers EQUAL 8 OR NOT preflight_solver_threads EQUAL 8 OR
   NOT k16 EQUAL 16 OR NOT k16_fits OR NOT k32 EQUAL 32 OR NOT k32_fits OR
   NOT k64 EQUAL 64 OR k64_fits OR NOT abstract_actions LESS exact_actions OR
   k32_disk_bytes LESS k32_required_disk_bytes)
  message(FATAL_ERROR "TST bucketing preflight contract mismatch: ${tst}")
endif()

execute_process(
  COMMAND "${GTOSD_CLI}" postflop preflight-bucketing-gto-plus
          "${GTOSD_AHK_FIXTURE}" "${turn_report}" 8,16,32 2000000000 10737418240 Js
  RESULT_VARIABLE turn_result
  OUTPUT_VARIABLE turn_output
  ERROR_VARIABLE turn_error)
if(NOT turn_result EQUAL 0)
  message(FATAL_ERROR
    "turn bucketing preflight failed (${turn_result})\n${turn_output}\n${turn_error}")
endif()
file(READ "${turn_report}" turn)
string(JSON starting_street GET "${turn}" starting_street)
string(JSON fixed_turn GET "${turn}" fixed_turn)
string(JSON turn_partitions GET "${turn}" feature_cache partition_count)
string(JSON turn_observations GET "${turn}" feature_cache observation_count)
string(JSON turn_cache_upper GET "${turn}" feature_cache serialized_bytes_upper_bound)
if(NOT starting_street STREQUAL "turn" OR NOT fixed_turn STREQUAL "Js" OR
   NOT turn_partitions GREATER 0 OR NOT turn_observations GREATER 0)
  message(FATAL_ERROR "turn bucketing preflight contract mismatch: ${turn}")
endif()

execute_process(
  COMMAND "${GTOSD_CLI}" postflop build-feature-cache-gto-plus
          "${GTOSD_AHK_FIXTURE}" "${turn_cache}" Js
  RESULT_VARIABLE cache_result
  OUTPUT_VARIABLE cache_output
  ERROR_VARIABLE cache_error)
if(NOT cache_result EQUAL 0 OR NOT EXISTS "${turn_cache}")
  message(FATAL_ERROR
    "GTO+ turn feature cache failed (${cache_result})\n${cache_output}\n${cache_error}")
endif()
file(SIZE "${turn_cache}" turn_cache_actual)
if(turn_cache_actual GREATER turn_cache_upper)
  message(FATAL_ERROR
    "feature-cache upper bound ${turn_cache_upper} is below actual ${turn_cache_actual}")
endif()
execute_process(
  COMMAND "${GTOSD_CLI}" postflop qualify-bucketing-gto-plus
          "${GTOSD_AHK_FIXTURE}" "${turn_cache}" "${turn_qualification}"
          64 100 2000000000 10737418240 Js
  RESULT_VARIABLE qualification_result
  OUTPUT_VARIABLE qualification_output
  ERROR_VARIABLE qualification_error)
if(NOT qualification_result EQUAL 0)
  message(FATAL_ERROR
    "GTO+ turn qualification failed (${qualification_result})\n${qualification_output}\n${qualification_error}")
endif()
file(READ "${turn_qualification}" qualification)
string(JSON qualification_schema GET "${qualification}" schema)
string(JSON exact_br GET "${qualification}" exact_combo_best_response)
string(JSON cache_reused GET "${qualification}" feature_cache_reused)
string(JSON averaging_delay GET "${qualification}" averaging_delay)
string(JSON compression GET "${qualification}" compression_ratio)
string(JSON solver_threads GET "${qualification}" solver_threads)
string(JSON parallel_workers GET "${qualification}" parallel_action_workers)
if(NOT qualification_schema STREQUAL "gtosd.card_abstraction_qualification.v1" OR
   NOT exact_br OR NOT cache_reused OR NOT averaging_delay EQUAL 0 OR
   NOT compression EQUAL 1 OR NOT solver_threads EQUAL 8 OR NOT parallel_workers EQUAL 7)
  message(FATAL_ERROR "GTO+ turn qualification contract mismatch: ${qualification}")
endif()

file(REMOVE "${tst_report}" "${turn_report}" "${turn_cache}" "${turn_qualification}")
