if(NOT DEFINED GTOSD_FIXTURE_DIR OR NOT DEFINED GTOSD_RUNNER)
  message(FATAL_ERROR "missing fixture directory or runner")
endif()

file(READ "${GTOSD_RUNNER}" runner)
foreach(token
    "Measure-Object -Maximum"
    "$gtoPlusSolverMemoryReferenceBytes"
    "gtosd.gto_plus_convergence_summary.v4"
    "gto_plus_metric_semantics_unresolved"
    "active_gate_passed")
  string(FIND "${runner}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "runner memory contract token missing: ${token}")
  endif()
endforeach()
foreach(forbidden_token
    "$referencePeakRssBytes"
    "$memoryGatePassed"
    "$desktopMemoryGatePassed"
    "desktop_peak_rss_cap_bytes")
  string(FIND "${runner}" "${forbidden_token}" token_index)
  if(NOT token_index EQUAL -1)
    message(FATAL_ERROR "runner retains forbidden legacy token: ${forbidden_token}")
  endif()
endforeach()

set(fixtures
  gto_plus_ahkhqh_101.json
  gto_plus_th7d6s_101.json
  gto_plus_tstc9d_101.json)
set(reference_times 1.71 17.66 116.09)
set(reference_display_values 8.0 399 2000)
set(reference_solver_memory 8000000 399000000 2000000000)

foreach(index RANGE 0 2)
  list(GET fixtures ${index} fixture)
  list(GET reference_times ${index} reference_time)
  list(GET reference_display_values ${index} reference_display_value)
  list(GET reference_solver_memory ${index} reference_bytes)
  file(READ "${GTOSD_FIXTURE_DIR}/${fixture}" document)

  string(JSON schema GET "${document}" schema)
  string(JSON algorithm GET "${document}" gtosd_run algorithm)
  string(JSON alpha GET "${document}" gtosd_run dcfr_positive_regret_exponent)
  string(JSON gamma GET "${document}" gtosd_run dcfr_average_exponent)
  string(JSON delay GET "${document}" gtosd_run averaging_delay)
  string(JSON precision GET "${document}" gtosd_run state_precision)
  string(JSON parallel_depth GET "${document}" gtosd_run parallel_action_depth)
  string(JSON maximum_threads GET "${document}" gtosd_run maximum_solver_threads)
  string(JSON decision_scales GET "${document}" expected_layout decision_node_scales)
  string(JSON actual_reference_time GET "${document}" gto_plus_reference elapsed_seconds)
  string(JSON memory_label GET "${document}" gto_plus_reference solver_memory display_label)
  string(JSON memory_display_value GET "${document}"
    gto_plus_reference solver_memory display_value)
  string(JSON memory_display_unit GET "${document}"
    gto_plus_reference solver_memory display_unit)
  string(JSON actual_reference_memory GET "${document}"
    gto_plus_reference solver_memory normalized_reference_bytes)
  string(JSON memory_normalization GET "${document}"
    gto_plus_reference solver_memory normalization_rule)
  string(JSON memory_semantic_class GET "${document}"
    gto_plus_reference solver_memory semantic_class)
  string(JSON memory_comparability GET "${document}"
    gto_plus_reference solver_memory comparability_status)
  string(JSON legacy_peak_type ERROR_VARIABLE legacy_peak_error
    TYPE "${document}" gto_plus_reference peak_rss_bytes)
  string(JSON legacy_reference_type ERROR_VARIABLE legacy_reference_error
    TYPE "${document}" gto_plus_reference solver_memory_bytes)
  string(JSON run_cap_type ERROR_VARIABLE run_cap_error
    TYPE "${document}" gtosd_run peak_rss_cap_bytes)
  string(JSON target GET "${document}" gto_plus_reference target_dev_percent)

  if(NOT schema STREQUAL "gtosd.gto_plus_convergence_benchmark.v4" OR
     NOT algorithm STREQUAL "production_dcfr" OR
     NOT alpha EQUAL 1.5 OR
     NOT gamma EQUAL 3 OR
     NOT delay EQUAL 0 OR
     NOT precision STREQUAL "scaled_uint16_regret_strategy" OR
     NOT parallel_depth EQUAL 7 OR
     NOT maximum_threads EQUAL 8)
    message(FATAL_ERROR "${fixture}: qualified production DCFR contract drifted")
  endif()
  if(decision_scales LESS 1)
    message(FATAL_ERROR "${fixture}: missing signed-state scale golden")
  endif()
  if(NOT actual_reference_time EQUAL reference_time OR
     NOT memory_label STREQUAL "Memory needed for solving" OR
     NOT memory_display_value EQUAL reference_display_value OR
     NOT memory_display_unit STREQUAL "MB" OR
     NOT actual_reference_memory EQUAL reference_bytes OR
     NOT memory_normalization STREQUAL "decimal_mb_fixture_convention" OR
     NOT memory_semantic_class STREQUAL "gto_plus_internal_pre_solve_estimate" OR
     NOT memory_comparability STREQUAL "unresolved" OR
     NOT target EQUAL 1)
    message(FATAL_ERROR "${fixture}: immutable GTO+ reference drifted")
  endif()
  if(NOT legacy_peak_error OR NOT legacy_reference_error OR NOT run_cap_error)
    message(FATAL_ERROR "${fixture}: legacy/common memory fields must not appear in v4")
  endif()

  if(index EQUAL 2)
    string(JSON initial_pot GET "${document}" fixture initial_pot_antes)
    string(JSON crossing_dev_antes GET "${document}"
      gto_plus_reference first_strictly_below_target dev_antes)
    string(JSON crossing_dev_percent GET "${document}"
      gto_plus_reference first_strictly_below_target dev_percent)
    if(NOT initial_pot EQUAL 16 OR
       NOT crossing_dev_antes EQUAL 0.146 OR
       NOT crossing_dev_percent EQUAL 0.91)
      message(FATAL_ERROR
        "${fixture}: TST dEV normalization contract drifted (0.146 / 16 = 0.9125%)")
    endif()
  endif()
endforeach()
