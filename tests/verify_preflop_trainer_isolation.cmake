if(NOT DEFINED GTOSD_SOURCE_DIR)
  message(FATAL_ERROR "GTOSD_SOURCE_DIR is required")
endif()

set(PREFLOP_INCLUDE "${GTOSD_SOURCE_DIR}/include/gtosd/preflop/hu_preflop.hpp")
set(PREFLOP_CMAKE "${GTOSD_SOURCE_DIR}/libs/preflop/CMakeLists.txt")
set(TRAINER_BENCHMARK "${GTOSD_SOURCE_DIR}/benchmarks/hu_preflop_trainer.cpp")
set(TRAINER_TEST "${GTOSD_SOURCE_DIR}/tests/hu_preflop_trainer_isolation_tests.cpp")

file(READ "${PREFLOP_INCLUDE}" preflop_header)
file(READ "${PREFLOP_CMAKE}" preflop_cmake)

if(preflop_header MATCHES "gtosd/postflop/|gtosd_postflop")
  message(FATAL_ERROR "the light preflop header includes the standalone postflop contract")
endif()
if(preflop_cmake MATCHES "add_library\\(gtosd_preflop_trainer[^)]*gtosd_postflop")
  message(FATAL_ERROR "the trainer target links the standalone postflop solver")
endif()
if(NOT preflop_cmake MATCHES "add_library\\(gtosd_preflop_trainer")
  message(FATAL_ERROR "the autonomous trainer target is missing")
endif()
if(preflop_cmake MATCHES "gtosd_preflop_trainer[^)]*hu_preflop_decomposition\\.cpp")
  message(FATAL_ERROR "the trainer target includes the legacy decomposition adapter")
endif()
if(preflop_cmake MATCHES "gtosd_preflop_trainer[^)]*hu_preflop_sampled_evaluation\\.cpp")
  message(FATAL_ERROR "the trainer target includes the postflop policy evaluator")
endif()

foreach(path IN ITEMS
    "${GTOSD_SOURCE_DIR}/libs/preflop/src/config.cpp"
    "${GTOSD_SOURCE_DIR}/libs/preflop/src/hu_preflop.cpp"
    "${GTOSD_SOURCE_DIR}/libs/preflop/src/hu_preflop_persistence.cpp"
    "${GTOSD_SOURCE_DIR}/libs/preflop/src/hu_preflop_solver.cpp"
    "${TRAINER_BENCHMARK}"
    "${TRAINER_TEST}")
  file(READ "${path}" source)
  if(source MATCHES "gtosd/postflop/|gtosd_postflop")
    message(FATAL_ERROR "forbidden postflop dependency in ${path}")
  endif()
endforeach()

message(STATUS "HU_PREFLOP_TRAINER_DEPENDENCY_CHECK=PASS")
