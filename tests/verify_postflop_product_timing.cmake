if(NOT DEFINED GTOSD_CLI OR NOT DEFINED GTOSD_CONFIG OR
   NOT DEFINED GTOSD_OUTPUT_DIR)
  message(FATAL_ERROR "missing product-timing smoke-test argument")
endif()

file(MAKE_DIRECTORY "${GTOSD_OUTPUT_DIR}")
set(checkpoint "${GTOSD_OUTPUT_DIR}/solve.chk")
set(report_prefix "${GTOSD_OUTPUT_DIR}/solve")
file(REMOVE "${checkpoint}" "${checkpoint}.control"
  "${report_prefix}.json" "${report_prefix}.md")

execute_process(
  COMMAND "${GTOSD_CLI}" postflop solve "${GTOSD_CONFIG}" 20
    "${checkpoint}" "${report_prefix}" 31 64 20
  RESULT_VARIABLE solve_result
  OUTPUT_VARIABLE solve_output
  ERROR_VARIABLE solve_error)
if(NOT solve_result EQUAL 0)
  message(FATAL_ERROR
    "product solve failed (${solve_result})\n${solve_output}\n${solve_error}")
endif()
if(NOT EXISTS "${checkpoint}" OR NOT EXISTS "${report_prefix}.json")
  message(FATAL_ERROR "product solve did not persist consultable outputs")
endif()

file(READ "${report_prefix}.json" report_json)
string(JSON schema GET "${report_json}" schema_version)
string(JSON algorithm GET "${report_json}" algorithm)
string(JSON precision GET "${report_json}" state_precision)
string(JSON startup GET "${report_json}" timer_scope process_startup)
string(JSON game_work GET "${report_json}" timer_scope game_specific_work)
string(JSON elapsed GET "${report_json}" elapsed_seconds)
string(JSON build_ready GET "${report_json}" build_to_ready_seconds)
string(JSON solve_consultable GET "${report_json}" solve_to_consultable_seconds)
string(JSON build_consultable GET "${report_json}" build_to_consultable_seconds)
if(NOT schema EQUAL 2 OR
   NOT algorithm STREQUAL "production_dcfr" OR
   NOT precision STREQUAL "scaled_uint16_regret_strategy" OR
   startup OR NOT game_work OR
   elapsed LESS_EQUAL 0 OR build_ready LESS_EQUAL 0 OR
   solve_consultable LESS elapsed OR
   build_consultable LESS solve_consultable OR
   build_consultable LESS build_ready)
  message(FATAL_ERROR "product timing or production profile contract drifted")
endif()
string(FIND "${solve_output}" "GTOSD_POSTFLOP_SOLVE_1" marker)
if(marker EQUAL -1)
  message(FATAL_ERROR "product solve marker is missing")
endif()
