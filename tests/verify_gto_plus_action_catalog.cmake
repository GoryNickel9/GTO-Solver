if(NOT DEFINED GTOSD_CLI OR NOT DEFINED GTOSD_FIXTURE OR NOT DEFINED GTOSD_REPORT)
  message(FATAL_ERROR "missing action-catalog smoke-test argument")
endif()

execute_process(
  COMMAND "${GTOSD_CLI}" postflop benchmark-gto-plus "${GTOSD_FIXTURE}" "${GTOSD_REPORT}"
  RESULT_VARIABLE benchmark_result
  OUTPUT_VARIABLE benchmark_output
  ERROR_VARIABLE benchmark_error)
if(NOT benchmark_result EQUAL 0 AND NOT benchmark_result EQUAL 4)
  message(FATAL_ERROR
    "benchmark command failed (${benchmark_result})\n${benchmark_output}\n${benchmark_error}")
endif()
string(FIND "${benchmark_output}" "GTOSD_GTO_PLUS_CONVERGENCE_RUN_3" run_marker_index)
if(run_marker_index EQUAL -1)
  message(FATAL_ERROR "benchmark run marker schema drifted")
endif()

file(READ "${GTOSD_REPORT}" report_json)
string(JSON report_schema GET "${report_json}" schema)
string(JSON state_metric GET "${report_json}" solver_state_gate metric)
string(JSON state_reference GET "${report_json}" solver_state_gate reference_bytes)
string(JSON state_pass GET "${report_json}" solver_state_gate passed)
string(JSON state_reference_basis GET "${report_json}" solver_state_gate reference_basis)
string(JSON external_metric GET "${report_json}" gto_plus_reference_memory metric)
string(JSON external_reference GET "${report_json}"
  gto_plus_reference_memory reference_bytes)
string(JSON memory_metric GET "${report_json}" memory_gate metric)
string(JSON peak_rss GET "${report_json}" memory_gate measured_bytes)
string(JSON memory_reference GET "${report_json}" memory_gate reference_bytes)
string(JSON memory_unit GET "${report_json}" memory_gate reference_unit)
string(JSON memory_comparison GET "${report_json}" memory_gate comparison)
string(JSON memory_pass GET "${report_json}" memory_gate passed)
string(JSON desktop_metric GET "${report_json}" desktop_memory_gate metric)
string(JSON desktop_cap GET "${report_json}" desktop_memory_gate cap_bytes)
string(JSON desktop_unit GET "${report_json}" desktop_memory_gate cap_unit)
string(JSON desktop_comparison GET "${report_json}" desktop_memory_gate comparison)
string(JSON desktop_pass GET "${report_json}" desktop_memory_gate passed)
if(NOT report_schema STREQUAL "gtosd.gto_plus_convergence_run.v3" OR
   NOT state_metric STREQUAL "solver_state_bytes" OR
   NOT state_reference EQUAL 8000000 OR
   NOT state_reference_basis STREQUAL "gto_plus_peak_rss_bytes" OR
   NOT state_pass OR
   NOT external_metric STREQUAL "peak_rss_bytes" OR
   NOT external_reference EQUAL 8000000 OR
   NOT memory_metric STREQUAL "peak_rss_bytes" OR
   NOT memory_reference EQUAL 8000000 OR
   NOT memory_unit STREQUAL "bytes" OR
   NOT memory_comparison STREQUAL "less_than_or_equal" OR
   memory_pass OR
   NOT peak_rss GREATER 8000000 OR
   NOT desktop_metric STREQUAL "peak_rss_bytes" OR
   NOT desktop_cap EQUAL 2147483648 OR
   NOT desktop_unit STREQUAL "GiB" OR
   NOT desktop_comparison STREQUAL "strict_less_than" OR
   NOT desktop_pass)
  message(FATAL_ERROR "state/reference/peak-RSS resource contract drifted")
endif()

string(JSON catalog_type TYPE "${report_json}" initial_street_action_catalog)
if(NOT catalog_type STREQUAL "ARRAY")
  message(FATAL_ERROR "initial_street_action_catalog is not an array")
endif()

string(JSON catalog_length LENGTH "${report_json}" initial_street_action_catalog)
if(catalog_length LESS 1)
  message(FATAL_ERROR "initial_street_action_catalog is empty")
endif()

string(JSON root_history_length LENGTH "${report_json}" initial_street_action_catalog 0 history)
if(NOT root_history_length EQUAL 0)
  message(FATAL_ERROR "first catalog entry is not the root decision")
endif()

string(JSON root_action_count LENGTH "${report_json}" initial_street_action_catalog 0 actions)
if(NOT root_action_count EQUAL 2)
  message(FATAL_ERROR "unexpected root action count: ${root_action_count}")
endif()

string(JSON check_label GET "${report_json}" initial_street_action_catalog 0 actions 0 label)
string(JSON bet_label GET "${report_json}" initial_street_action_catalog 0 actions 1 label)
string(JSON bet_amount_units GET "${report_json}" initial_street_action_catalog 0 actions 1 amount_units)
string(JSON bet_requested_basis_points GET "${report_json}" initial_street_action_catalog 0 actions 1 requested_basis_points)
if(NOT check_label STREQUAL "check")
  message(FATAL_ERROR "unexpected first root action: ${check_label}")
endif()
if(NOT bet_label STREQUAL "bet_20")
  message(FATAL_ERROR "legacy action label changed: ${bet_label}")
endif()
if(NOT bet_amount_units EQUAL 200000)
  message(FATAL_ERROR "exact 50%-pot amount changed: ${bet_amount_units}")
endif()
if(NOT bet_requested_basis_points EQUAL 5000)
  message(FATAL_ERROR "requested sizing changed: ${bet_requested_basis_points}")
endif()
