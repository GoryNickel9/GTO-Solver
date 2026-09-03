if(NOT DEFINED GTOSD_FIXTURE_DIR OR NOT DEFINED GTOSD_RUNNER)
  message(FATAL_ERROR "missing fixture directory or runner")
endif()

file(READ "${GTOSD_RUNNER}" runner)
foreach(token
    "Measure-Object -Maximum"
    "$referencePeakRssBytes"
    "$peakRssBytes -le $memoryGateBytes"
    "gtosd.gto_plus_convergence_summary.v3"
    "$desktopMemoryGatePassed")
  string(FIND "${runner}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "runner memory contract token missing: ${token}")
  endif()
endforeach()

set(fixtures
  gto_plus_ahkhqh_101.json
  gto_plus_th7d6s_101.json
  gto_plus_tstc9d_101.json)
set(reference_times 1.71 17.66 116.09)
set(reference_peak_rss 8000000 399000000 2000000000)

foreach(index RANGE 0 2)
  list(GET fixtures ${index} fixture)
  list(GET reference_times ${index} reference_time)
  list(GET reference_peak_rss ${index} reference_bytes)
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
  string(JSON actual_reference_peak_rss GET "${document}" gto_plus_reference peak_rss_bytes)
  string(JSON legacy_reference_type ERROR_VARIABLE legacy_reference_error
    TYPE "${document}" gto_plus_reference solver_memory_bytes)
  string(JSON run_cap_type ERROR_VARIABLE run_cap_error
    TYPE "${document}" gtosd_run peak_rss_cap_bytes)
  string(JSON target GET "${document}" gto_plus_reference target_dev_percent)

  if(NOT schema STREQUAL "gtosd.gto_plus_convergence_benchmark.v3" OR
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
     NOT actual_reference_peak_rss EQUAL reference_bytes OR
     NOT target EQUAL 1)
    message(FATAL_ERROR "${fixture}: immutable GTO+ reference drifted")
  endif()
  if(NOT legacy_reference_error OR NOT run_cap_error)
    message(FATAL_ERROR "${fixture}: legacy/common memory fields must not appear in v3")
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
