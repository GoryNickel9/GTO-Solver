if(NOT DEFINED GTOSD_CLI OR NOT DEFINED GTOSD_FIXTURE OR NOT DEFINED GTOSD_OUTPUT_DIR)
  message(FATAL_ERROR "GTOSD_CLI, GTOSD_FIXTURE and GTOSD_OUTPUT_DIR are required")
endif()

set(cache "${GTOSD_OUTPUT_DIR}/bucketed_subgame_cli.features")
set(blueprint "${GTOSD_OUTPUT_DIR}/bucketed_subgame_blueprint.chk")
set(resolved "${GTOSD_OUTPUT_DIR}/bucketed_subgame_resolved.chk")
set(solve_report "${GTOSD_OUTPUT_DIR}/bucketed_subgame_blueprint")
set(resolve_report "${GTOSD_OUTPUT_DIR}/bucketed_subgame_report.json")
file(REMOVE "${cache}" "${cache}.tmp" "${blueprint}" "${blueprint}.tmp" "${resolved}"
            "${resolved}.tmp" "${solve_report}.json" "${solve_report}.md" "${resolve_report}")

execute_process(
  COMMAND "${GTOSD_CLI}" postflop build-feature-cache "${GTOSD_FIXTURE}" "${cache}"
  RESULT_VARIABLE cache_result
  OUTPUT_VARIABLE cache_output
  ERROR_VARIABLE cache_error)
if(NOT cache_result EQUAL 0)
  message(FATAL_ERROR "cache build failed (${cache_result})\n${cache_output}\n${cache_error}")
endif()

execute_process(
  COMMAND "${GTOSD_CLI}" postflop edges-bucketed "${GTOSD_FIXTURE}" 8 0 "${cache}"
  RESULT_VARIABLE edges_result
  OUTPUT_VARIABLE edges_output
  ERROR_VARIABLE edges_error)
if(NOT edges_result EQUAL 0)
  message(FATAL_ERROR "edge navigation failed (${edges_result})\n${edges_output}\n${edges_error}")
endif()
string(JSON edges_schema GET "${edges_output}" schema)
string(JSON edges_node GET "${edges_output}" node)
string(JSON edges_count LENGTH "${edges_output}" edges)
string(JSON first_edge GET "${edges_output}" edges 0 edge_index)
string(JSON first_outcome GET "${edges_output}" edges 0 outcomes 0 outcome_index)
if(NOT edges_schema STREQUAL "gtosd.postflop.bucketed-edges.v1" OR NOT edges_node EQUAL 0 OR
   NOT edges_count GREATER 0 OR NOT first_edge EQUAL 0 OR NOT first_outcome EQUAL 0)
  message(FATAL_ERROR "edge navigation contract mismatch: ${edges_output}")
endif()

execute_process(
  COMMAND "${GTOSD_CLI}" postflop solve-bucketed "${GTOSD_FIXTURE}" 100 "${blueprint}"
          "${solve_report}" 16 16 8 100 "${cache}"
  RESULT_VARIABLE solve_result
  OUTPUT_VARIABLE solve_output
  ERROR_VARIABLE solve_error)
if(NOT solve_result EQUAL 0)
  message(FATAL_ERROR "blueprint solve failed (${solve_result})\n${solve_output}\n${solve_error}")
endif()

# Edge 0 at the fixed-river root is check; its sole outcome enters the BTN
# response decision and therefore forms a proper infoset-closed subgame.
execute_process(
  COMMAND "${GTOSD_CLI}" postflop resolve-bucketed "${GTOSD_FIXTURE}" "${blueprint}"
          "${resolved}" 8 0:0 100 17179869184 17179869184 1073741824
          "${resolve_report}" "${cache}"
  RESULT_VARIABLE resolve_result
  OUTPUT_VARIABLE resolve_output
  ERROR_VARIABLE resolve_error)
if(NOT resolve_result EQUAL 0 OR NOT EXISTS "${resolved}" OR NOT EXISTS "${resolve_report}" OR
   NOT resolve_output MATCHES "solver_threads=8")
  message(FATAL_ERROR
    "bucketed subgame resolve failed (${resolve_result})\n${resolve_output}\n${resolve_error}")
endif()

file(READ "${resolve_report}" report)
string(JSON schema GET "${report}" schema)
string(JSON algorithm GET "${report}" algorithm)
string(JSON solver_threads GET "${report}" solver_threads)
string(JSON deployment GET "${report}" deployment)
string(JSON root GET "${report}" canonical_root)
string(JSON reach GET "${report}" public_reach_probability)
string(JSON decisions GET "${report}" affected_decision_nodes)
string(JSON actions GET "${report}" affected_action_entries)
string(JSON snapshot GET "${report}" rollback_snapshot_bytes)
string(JSON baseline GET "${report}" baseline normalized_nash_conv)
string(JSON candidate GET "${report}" candidate normalized_nash_conv)
string(JSON deployed GET "${report}" deployed normalized_nash_conv)
math(EXPR expected_snapshot "${actions} * 16")
if(NOT schema STREQUAL "gtosd.postflop.bucketed-subgame-resolution.v1" OR
   NOT algorithm STREQUAL "cfr_plus" OR NOT solver_threads EQUAL 8 OR NOT root GREATER 0 OR
   NOT reach GREATER 0 OR NOT decisions GREATER 0 OR NOT actions GREATER 0 OR
   NOT snapshot EQUAL expected_snapshot)
  message(FATAL_ERROR "bucketed subgame report contract mismatch: ${report}")
endif()
if(deployment STREQUAL "candidate_accepted")
  if(candidate GREATER baseline OR NOT deployed EQUAL candidate)
    message(FATAL_ERROR "unsafe accepted candidate: ${report}")
  endif()
elseif(deployment STREQUAL "blueprint_fallback")
  if(NOT candidate GREATER baseline OR NOT deployed EQUAL baseline)
    message(FATAL_ERROR "invalid fallback metrics: ${report}")
  endif()
else()
  message(FATAL_ERROR "unknown deployment: ${deployment}")
endif()

execute_process(
  COMMAND "${GTOSD_CLI}" postflop certify-bucketed "${GTOSD_FIXTURE}" "${resolved}" 8 "${cache}"
  RESULT_VARIABLE certify_result
  OUTPUT_VARIABLE certify_output
  ERROR_VARIABLE certify_error)
if(NOT certify_result EQUAL 0)
  message(FATAL_ERROR
    "resolved checkpoint certification failed (${certify_result})\n${certify_output}\n${certify_error}")
endif()

execute_process(
  COMMAND "${GTOSD_CLI}" postflop resolve-bucketed "${GTOSD_FIXTURE}" "${blueprint}"
          "${blueprint}" 8 0:0 10 17179869184 17179869184 1073741824
          "${resolve_report}" "${cache}"
  RESULT_VARIABLE overwrite_result
  OUTPUT_QUIET
  ERROR_QUIET)
if(overwrite_result EQUAL 0)
  message(FATAL_ERROR "resolve-bucketed overwrote its source blueprint")
endif()

file(REMOVE "${cache}" "${blueprint}" "${resolved}" "${solve_report}.json"
            "${solve_report}.md" "${resolve_report}")
