# Dependency guard for the preflop blueprint libraries (roadmap P0, section 2.2).
#
# The new targets may depend only on core, equity, tree and card_abstraction.
# Any reference to the standalone postflop solver, to the legacy preflop
# trainer or to the other engine libraries fails this check. Tests and oracles
# are intentionally outside the guarded set: they may link gtosd::solver and,
# in the test tree only, the postflop solver used as an oracle.

if(NOT DEFINED GTOSD_SOURCE_DIR)
  message(FATAL_ERROR "GTOSD_SOURCE_DIR is required")
endif()

set(GUARDED_CMAKE
  "${GTOSD_SOURCE_DIR}/libs/card_abstraction/CMakeLists.txt"
  "${GTOSD_SOURCE_DIR}/libs/preflop_blueprint/CMakeLists.txt")

file(GLOB_RECURSE GUARDED_SOURCES
  "${GTOSD_SOURCE_DIR}/libs/card_abstraction/src/*.cpp"
  "${GTOSD_SOURCE_DIR}/libs/card_abstraction/src/*.hpp"
  "${GTOSD_SOURCE_DIR}/libs/preflop_blueprint/src/*.cpp"
  "${GTOSD_SOURCE_DIR}/libs/preflop_blueprint/src/*.hpp"
  "${GTOSD_SOURCE_DIR}/include/gtosd/card_abstraction/*.hpp"
  "${GTOSD_SOURCE_DIR}/include/gtosd/preflop_blueprint/*.hpp")
file(GLOB GUARDED_BENCHMARKS "${GTOSD_SOURCE_DIR}/benchmarks/preflop_blueprint_*.cpp")

if(NOT GUARDED_SOURCES)
  message(FATAL_ERROR "no preflop blueprint sources found under ${GTOSD_SOURCE_DIR}")
endif()

set(FORBIDDEN_LINK_TARGETS
  "gtosd::solver" "gtosd::best_response" "gtosd::solver_validation" "gtosd::isomorphism"
  "gtosd::memory" "gtosd::postflop" "gtosd::postflop_subgame" "gtosd::preflop"
  "gtosd::preflop_trainer" "gtosd::preflop_certifier" "gtosd::storage"
  "gtosd_solver" "gtosd_best_response" "gtosd_solver_validation" "gtosd_isomorphism"
  "gtosd_memory" "gtosd_postflop" "gtosd_postflop_subgame" "gtosd_preflop_trainer"
  "gtosd_preflop_certifier" "gtosd_storage")

foreach(path IN LISTS GUARDED_CMAKE)
  file(READ "${path}" content)
  # The blueprint target names contain the legacy prefix; neutralize them
  # before searching so only the legacy names can match.
  string(REPLACE "preflop_blueprint" "PB" content "${content}")
  foreach(forbidden IN LISTS FORBIDDEN_LINK_TARGETS)
    string(FIND "${content}" "${forbidden}" position)
    if(NOT position EQUAL -1)
      message(FATAL_ERROR "forbidden link target ${forbidden} in ${path}")
    endif()
  endforeach()
  if(content MATCHES "gtosd_preflop([^_A-Za-z]|$)")
    message(FATAL_ERROR "forbidden legacy preflop target in ${path}")
  endif()
endforeach()

foreach(path IN LISTS GUARDED_SOURCES GUARDED_BENCHMARKS)
  file(READ "${path}" source)
  string(REPLACE "preflop_blueprint" "PB" source "${source}")
  if(source MATCHES "gtosd/postflop/|gtosd_postflop|gtosd/preflop/|gtosd/solver/|gtosd/memory/|gtosd/storage/|gtosd/isomorphism/|hu_preflop")
    message(FATAL_ERROR "forbidden dependency in ${path}")
  endif()
endforeach()

list(LENGTH GUARDED_SOURCES source_count)
message(STATUS "PREFLOP_BLUEPRINT_DEPENDENCY_CHECK=PASS sources=${source_count}")
