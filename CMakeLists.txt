cmake_minimum_required(VERSION 3.25)

project(
  gtosd
  VERSION 0.7.0
  DESCRIPTION "Exact Short Deck game and solver core"
  LANGUAGES CXX)

option(GTOSD_BUILD_TESTS "Build first-party tests" ON)
option(GTOSD_BUILD_BENCHMARKS "Build benchmark smoke targets" ON)
option(GTOSD_BUILD_FRAMEWORK_TESTS "Build GoogleTest infrastructure tests" ON)
option(GTOSD_BUILD_NIGHTLY_TESTS "Build deterministic million-deal tests" OFF)
option(GTOSD_WARNINGS_AS_ERRORS "Treat first-party warnings as errors" ON)
option(GTOSD_ENABLE_CLANG_TIDY "Run clang-tidy while compiling first-party targets" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

configure_file(
  ${PROJECT_SOURCE_DIR}/include/gtosd/version.hpp.in
  ${PROJECT_BINARY_DIR}/generated/gtosd/version.hpp
  @ONLY)

if(GTOSD_ENABLE_CLANG_TIDY)
  find_program(GTOSD_CLANG_TIDY_EXECUTABLE NAMES clang-tidy
    HINTS "$ENV{VCINSTALLDIR}/Tools/Llvm/x64/bin"
    REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  set(CMAKE_CXX_CLANG_TIDY
    ${GTOSD_CLANG_TIDY_EXECUTABLE}
    --config-file=${PROJECT_SOURCE_DIR}/.clang-tidy)
  if(MSVC)
    list(APPEND CMAKE_CXX_CLANG_TIDY --extra-arg=/EHsc)
  endif()
endif()

function(gtosd_set_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
    if(GTOSD_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(GTOSD_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

if(MSVC)
  get_filename_component(GTOSD_COMPILER_RUNTIME_DIR
    "${CMAKE_CXX_COMPILER}" DIRECTORY)
  set(GTOSD_TEST_RUNTIME_ENVIRONMENT
    "PATH=path_list_prepend:${GTOSD_COMPILER_RUNTIME_DIR}")
endif()

file(GLOB_RECURSE GTOSD_FORMAT_FILES CONFIGURE_DEPENDS
  ${PROJECT_SOURCE_DIR}/apps/*.cpp
  ${PROJECT_SOURCE_DIR}/benchmarks/*.cpp
  ${PROJECT_SOURCE_DIR}/include/*.hpp
  ${PROJECT_SOURCE_DIR}/libs/*.cpp
  ${PROJECT_SOURCE_DIR}/tests/*.cpp)
find_program(GTOSD_CLANG_FORMAT_EXECUTABLE NAMES clang-format
  HINTS "$ENV{VCINSTALLDIR}/Tools/Llvm/x64/bin"
  NO_CMAKE_FIND_ROOT_PATH)
if(GTOSD_CLANG_FORMAT_EXECUTABLE)
  add_custom_target(format-check
    COMMAND ${GTOSD_CLANG_FORMAT_EXECUTABLE} --dry-run --Werror ${GTOSD_FORMAT_FILES}
    COMMENT "Checking first-party C++ formatting"
    VERBATIM)
endif()

add_subdirectory(libs/core)
add_subdirectory(libs/equity)
add_subdirectory(libs/tree)
add_subdirectory(libs/isomorphism)
add_subdirectory(libs/solver)
add_subdirectory(libs/best_response)
add_subdirectory(libs/memory)
add_subdirectory(libs/postflop)
add_subdirectory(apps/gto_cli)

if(GTOSD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(GTOSD_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

include(GNUInstallDirs)
install(TARGETS
    gtosd_core
    gtosd_equity
    gtosd_tree
    gtosd_isomorphism
    gtosd_solver
    gtosd_best_response
    gtosd_memory
    gtosd_postflop
    gto_cli
  EXPORT gtosdTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  PATTERN "*.in" EXCLUDE)
install(FILES ${PROJECT_BINARY_DIR}/generated/gtosd/version.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gtosd)
install(FILES schemas/postflop_tree_config.schema.json
              schemas/gto_plus_reference.schema.json
  DESTINATION ${CMAKE_INSTALL_DATADIR}/gtosd/schemas)
install(DIRECTORY benchmarks/fixtures/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/gtosd/benchmarks)
install(EXPORT gtosdTargets
  FILE gtosdTargets.cmake
  NAMESPACE gtosd::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gtosd)
