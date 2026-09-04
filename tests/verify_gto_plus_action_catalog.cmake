if(NOT DEFINED GTOSD_CLI OR NOT DEFINED GTOSD_FIXTURE OR NOT DEFINED GTOSD_REPORT)
  message(FATAL_ERROR "missing action-catalog smoke-test argument")
endif()

execute_process(
  COMMAND "${GTOSD_CLI}" postflop benchmark-gto-plus "${GTOSD_FIXTURE}" "${GTOSD_REPORT}"
  RESULT_VARIABLE benchmark_result
  OUTPUT_VARIABLE benchmark_output
  ERROR_VARIABLE benchmark_error)
if(NOT benchmark_result EQUAL 0)
  message(FATAL_ERROR
    "benchmark command failed (${benchmark_result})\n${benchmark_output}\n${benchmark_error}")
endif()
string(FIND "${benchmark_output}" "GTOSD_GTO_PLUS_CONVERGENCE_RUN_4" run_marker_index)
if(run_marker_index EQUAL -1)
  message(FATAL_ERROR "benchmark run marker schema drifted")
endif()

file(READ "${GTOSD_REPORT}" report_json)
string(JSON report_schema GET "${report_json}" schema)
string(JSON source_fixture_schema GET "${report_json}" source_fixture_schema)
string(JSON state_accounting_schema GET "${report_json}" solver_memory_accounting schema)
string(JSON state_logical_bytes GET "${report_json}" solver_memory_accounting state_logical_bytes)
string(JSON managed_payload_type TYPE "${report_json}"
  solver_memory_accounting managed_payload_peak_bytes)
string(JSON managed_allocated_type TYPE "${report_json}"
  solver_memory_accounting managed_allocated_peak_bytes)
string(JSON managed_payload GET "${report_json}"
  solver_memory_accounting managed_payload_peak_bytes)
string(JSON managed_allocated GET "${report_json}"
  solver_memory_accounting managed_allocated_peak_bytes)
string(JSON accounting_complete GET "${report_json}"
  solver_memory_accounting accounting_complete)
string(JSON accounting_categories_type TYPE "${report_json}"
  solver_memory_accounting categories)
string(JSON accounting_categories_length LENGTH "${report_json}"
  solver_memory_accounting categories)
string(JSON external_metric GET "${report_json}" gto_plus_reference_memory metric)
string(JSON external_label GET "${report_json}" gto_plus_reference_memory display_label)
string(JSON external_value GET "${report_json}" gto_plus_reference_memory display_value)
string(JSON external_unit GET "${report_json}" gto_plus_reference_memory display_unit)
string(JSON external_reference GET "${report_json}"
  gto_plus_reference_memory normalized_reference_bytes)
string(JSON external_normalization GET "${report_json}"
  gto_plus_reference_memory normalization_rule)
string(JSON external_semantic_class GET "${report_json}"
  gto_plus_reference_memory semantic_class)
string(JSON external_comparability GET "${report_json}"
  gto_plus_reference_memory comparability_status)
string(JSON peak_rss GET "${report_json}" process_memory peak_rss_bytes)
string(JSON process_gate_type TYPE "${report_json}" process_memory normative_gate)
string(JSON memory_status GET "${report_json}" memory_comparison status)
string(JSON memory_pass_type TYPE "${report_json}" memory_comparison passed)
string(JSON memory_reason GET "${report_json}" memory_comparison reason)
string(JSON state_residency GET "${report_json}" solver_state_residency)
string(JSON working_set_budget_type TYPE "${report_json}" resident_working_set_budget)
string(JSON materialization_required GET "${report_json}"
  runtime_state_materialization_required_for_persistence)
string(JSON legacy_state_gate_type ERROR_VARIABLE legacy_state_gate_error
  TYPE "${report_json}" solver_state_gate)
string(JSON legacy_memory_gate_type ERROR_VARIABLE legacy_memory_gate_error
  TYPE "${report_json}" memory_gate)
string(JSON legacy_desktop_gate_type ERROR_VARIABLE legacy_desktop_gate_error
  TYPE "${report_json}" desktop_memory_gate)
string(JSON legacy_peak_type ERROR_VARIABLE legacy_peak_error
  TYPE "${report_json}" peak_rss_bytes)
if(NOT report_schema STREQUAL "gtosd.gto_plus_convergence_run.v4" OR
   NOT source_fixture_schema STREQUAL "gtosd.gto_plus_convergence_benchmark.v4" OR
   NOT state_accounting_schema STREQUAL "gtosd.solver_memory_accounting.v2" OR
   NOT state_logical_bytes EQUAL 5300664 OR
   NOT managed_payload_type STREQUAL "NUMBER" OR
   NOT managed_allocated_type STREQUAL "NUMBER" OR
   managed_payload LESS state_logical_bytes OR
   managed_allocated LESS managed_payload OR
   NOT accounting_complete OR
   NOT accounting_categories_type STREQUAL "ARRAY" OR
   accounting_categories_length LESS 8 OR
   NOT external_metric STREQUAL "internal_solver_memory_estimate" OR
   NOT external_label STREQUAL "Memory needed for solving" OR
   NOT external_value EQUAL 8.0 OR
   NOT external_unit STREQUAL "MB" OR
   NOT external_reference EQUAL 8000000 OR
   NOT external_normalization STREQUAL "decimal_mb_fixture_convention" OR
   NOT external_semantic_class STREQUAL "gto_plus_internal_pre_solve_estimate" OR
   NOT external_comparability STREQUAL "unresolved" OR
   peak_rss LESS 1 OR
   NOT process_gate_type STREQUAL "NULL" OR
   NOT memory_status STREQUAL "not_evaluated" OR
   NOT memory_pass_type STREQUAL "NULL" OR
   NOT memory_reason STREQUAL "gto_plus_metric_semantics_unresolved" OR
   NOT state_residency STREQUAL "resident_vectors" OR
   NOT working_set_budget_type STREQUAL "NULL" OR
   materialization_required OR
   NOT legacy_state_gate_error OR
   NOT legacy_memory_gate_error OR
   NOT legacy_desktop_gate_error OR
   NOT legacy_peak_error)
  message(FATAL_ERROR "v4 solver/process/reference memory separation drifted")
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

# A v4 fixture must reject the legacy field instead of silently treating it as
# process Peak RSS or a working-set budget.
file(READ "${GTOSD_FIXTURE}" fixture_json)
get_filename_component(report_directory "${GTOSD_REPORT}" DIRECTORY)
set(invalid_v4_fixture "${report_directory}/gto_plus_invalid_v4_memory.json")
set(invalid_v4_report "${report_directory}/gto_plus_invalid_v4_memory_report.json")
string(JSON invalid_v4_json SET "${fixture_json}"
  gto_plus_reference peak_rss_bytes 8000000)
file(WRITE "${invalid_v4_fixture}" "${invalid_v4_json}\n")
execute_process(
  COMMAND "${GTOSD_CLI}" postflop benchmark-gto-plus
    "${invalid_v4_fixture}" "${invalid_v4_report}"
  RESULT_VARIABLE invalid_v4_result
  OUTPUT_VARIABLE invalid_v4_output
  ERROR_VARIABLE invalid_v4_error)
if(NOT invalid_v4_result EQUAL 2 OR
   NOT invalid_v4_error MATCHES "invalid_memory_reference")
  message(FATAL_ERROR
    "v4 legacy-memory field was not rejected (${invalid_v4_result}): "
    "${invalid_v4_output}${invalid_v4_error}")
endif()

# Legacy v3 remains readable, but the emitted v4 report must declare the old
# field misclassified and must not turn it into a solver residency budget.
set(legacy_v3_fixture "${report_directory}/gto_plus_legacy_v3_memory.json")
set(legacy_v3_report "${report_directory}/gto_plus_legacy_v3_memory_report.json")
string(JSON legacy_v3_json SET "${fixture_json}"
  schema "\"gtosd.gto_plus_convergence_benchmark.v3\"")
string(JSON legacy_v3_json REMOVE "${legacy_v3_json}"
  gto_plus_reference solver_memory)
string(JSON legacy_v3_json SET "${legacy_v3_json}"
  gto_plus_reference peak_rss_bytes 8000000)
string(JSON legacy_v3_json SET "${legacy_v3_json}"
  gto_plus_reference memory_unit "\"decimal_mb\"")
file(WRITE "${legacy_v3_fixture}" "${legacy_v3_json}\n")
execute_process(
  COMMAND "${GTOSD_CLI}" postflop benchmark-gto-plus
    "${legacy_v3_fixture}" "${legacy_v3_report}"
  RESULT_VARIABLE legacy_v3_result
  OUTPUT_VARIABLE legacy_v3_output
  ERROR_VARIABLE legacy_v3_error)
if(NOT legacy_v3_result EQUAL 0)
  message(FATAL_ERROR
    "legacy v3 conversion failed (${legacy_v3_result}): "
    "${legacy_v3_output}${legacy_v3_error}")
endif()
file(READ "${legacy_v3_report}" legacy_v3_report_json)
string(JSON legacy_report_schema GET "${legacy_v3_report_json}" schema)
string(JSON legacy_source_schema GET "${legacy_v3_report_json}" source_fixture_schema)
string(JSON legacy_comparability GET "${legacy_v3_report_json}"
  gto_plus_reference_memory comparability_status)
string(JSON legacy_reason GET "${legacy_v3_report_json}" memory_comparison reason)
string(JSON legacy_status GET "${legacy_v3_report_json}" memory_comparison status)
string(JSON legacy_pass_type TYPE "${legacy_v3_report_json}" memory_comparison passed)
string(JSON legacy_residency GET "${legacy_v3_report_json}" solver_state_residency)
string(JSON legacy_budget_type TYPE "${legacy_v3_report_json}" resident_working_set_budget)
if(NOT legacy_report_schema STREQUAL "gtosd.gto_plus_convergence_run.v4" OR
   NOT legacy_source_schema STREQUAL "gtosd.gto_plus_convergence_benchmark.v3" OR
   NOT legacy_comparability STREQUAL "legacy_metric_misclassified" OR
   NOT legacy_reason STREQUAL "legacy_metric_misclassified" OR
   NOT legacy_status STREQUAL "not_evaluated" OR
   NOT legacy_pass_type STREQUAL "NULL" OR
   NOT legacy_residency STREQUAL "resident_vectors" OR
   NOT legacy_budget_type STREQUAL "NULL")
  message(FATAL_ERROR "legacy v3 memory conversion semantics drifted")
endif()
