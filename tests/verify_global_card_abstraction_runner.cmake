if(NOT DEFINED GTOSD_GLOBAL_ABSTRACTION_RUNNER)
  message(FATAL_ERROR "missing global abstraction runner")
endif()

file(READ "${GTOSD_GLOBAL_ABSTRACTION_RUNNER}" runner)
foreach(token
    "one_k_for_every_fixture_worst_case_gate"
    "gtosd.global_card_abstraction_qualification.v1"
    "GTOSD_CARD_ABSTRACTION_DIAGNOSTIC_SOLVER_THREADS"
    "solver_threads = 8"
    "algorithm = 'cfr_plus'"
    "state_precision = 'float64'"
    "exact_combo_best_response = $true"
    "normalized_nash_conv_limit = 0.01"
    "REJECTED_GLOBAL"
    "QUALIFIED_GLOBAL"
    "source_commit"
    "executable_sha256")
  string(FIND "${runner}" "${token}" token_index)
  if(token_index EQUAL -1)
    message(FATAL_ERROR "global abstraction runner token missing: ${token}")
  endif()
endforeach()

foreach(forbidden_token
    "GTP-AHKHQH-101"
    "GTP-TH7D6S-101"
    "GTP-TSTC9D-101"
    "buckets_by_fixture"
    "fixture_bucket")
  string(FIND "${runner}" "${forbidden_token}" token_index)
  if(NOT token_index EQUAL -1)
    message(FATAL_ERROR "runner contains fixture-specific selection: ${forbidden_token}")
  endif()
endforeach()
