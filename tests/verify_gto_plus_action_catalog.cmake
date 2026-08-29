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

file(READ "${GTOSD_REPORT}" report_json)
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
