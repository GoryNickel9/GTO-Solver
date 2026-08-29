execute_process(
  COMMAND "${GTOSD_CLI}" postflop layout-gto-plus "${GTOSD_FIXTURE}" "${GTOSD_REPORT}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "layout preflight failed (${result}): ${output}${error}")
endif()

file(READ "${GTOSD_REPORT}" report)
string(JSON schema GET "${report}" schema)
string(JSON physical_nodes GET "${report}" physical_public_nodes)
string(JSON canonical_nodes GET "${report}" canonical_public_nodes)
string(JSON infosets GET "${report}" information_sets)
string(JSON actions GET "${report}" action_entries)
string(JSON automorphisms GET "${report}" preserving_suit_automorphisms)
string(JSON physical_match GET "${report}" physical_layout_matches_fixture)
string(JSON profile_count LENGTH "${report}" memory_model profiles)
string(JSON packed_peak_8 GET "${report}" memory_model profiles 3 estimated_peak_bytes)
string(JSON packed_target_8 GET "${report}" memory_model profiles 3 meets_engineering_target)
string(JSON scaled_peak_8 GET "${report}" memory_model profiles 7 estimated_peak_bytes)
string(JSON scaled_gate_8 GET "${report}" memory_model profiles 7 meets_absolute_gate)

if(NOT schema STREQUAL "gtosd.canonical_chance_layout.v1" OR
   NOT physical_nodes STREQUAL "2791872" OR
   NOT canonical_nodes STREQUAL "1758624" OR
   NOT infosets STREQUAL "145524152" OR
   NOT actions STREQUAL "366890152" OR
   NOT automorphisms STREQUAL "2" OR
   NOT physical_match OR
   NOT profile_count STREQUAL "8" OR
   NOT packed_peak_8 STREQUAL "1545640016" OR
   NOT packed_target_8 OR
   NOT scaled_peak_8 STREQUAL "1917574936" OR
   NOT scaled_gate_8)
  message(FATAL_ERROR "unexpected TSTC9D canonical layout report: ${report}")
endif()

message(STATUS "TSTC9D canonical layout verified: actions=${actions}, packed8=${packed_peak_8}")
