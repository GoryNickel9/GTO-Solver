cmake_minimum_required(VERSION 3.25)

project(
  gtosd
  VERSION 0.11.0
  DESCRIPTION "Short Deck game and solver core"
  LANGUAGES CXX)

option(GTOSD_BUILD_TESTS "Build first-party tests" ON)
option(GTOSD_BUILD_BENCHMARKS "Build benchmark smoke targets" ON)
option(GTOSD_BUILD_FRAMEWORK_TESTS "Build GoogleTest infrastructure tests" ON)
option(GTOSD_BUILD_NIGHTLY_TESTS "Build deterministic million-deal tests" OFF)
option(GTOSD_BUILD_GUI_PROTOTYPES "Build the Phase 9 Qt and Dear ImGui prototypes" OFF)
option(GTOSD_BUILD_DESKTOP_GUI "Build the Phase 10 Qt desktop application" OFF)
option(GTOSD_WARNINGS_AS_ERRORS "Treat first-party warnings as errors" ON)
option(GTOSD_ENABLE_CLANG_TIDY "Run clang-tidy while compiling first-party targets" OFF)
option(GTOSD_BUILD_RANGE_ORBIT_ORACLE
  "Build the diagnostic asymmetric-range physical-orbit counterexample" OFF)

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

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(GTOSD_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(libs/core)
add_subdirectory(libs/equity)
add_subdirectory(libs/tree)
add_subdirectory(libs/isomorphism)
add_subdirectory(libs/solver)
add_subdirectory(libs/best_response)
add_subdirectory(libs/abstraction)
add_subdirectory(libs/subgame)
add_subdirectory(libs/memory)
add_subdirectory(libs/postflop)
add_subdirectory(libs/storage)
add_subdirectory(libs/gui_prototype)
add_subdirectory(apps/gto_cli)
if(GTOSD_BUILD_GUI_PROTOTYPES)
  if(NOT WIN32)
    message(FATAL_ERROR "Phase 9 GUI prototypes currently target Windows only")
  endif()
  add_subdirectory(apps/gui_qt_prototype)
  add_subdirectory(apps/gui_imgui_prototype)
endif()
if(GTOSD_BUILD_DESKTOP_GUI)
  if(NOT WIN32)
    message(FATAL_ERROR "The Phase 10 desktop GUI currently targets Windows only")
  endif()
  add_subdirectory(apps/gto_gui)
endif()

if(GTOSD_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(GTOSD_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

install(TARGETS
    gtosd_core
    gtosd_equity
    gtosd_tree
    gtosd_isomorphism
    gtosd_solver
    gtosd_best_response
    gtosd_abstraction
    gtosd_subgame
    gtosd_memory
    gtosd_postflop
    gtosd_storage
    gtosd_gui_prototype
    gto_cli
  EXPORT gtosdTargets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  PATTERN "*.in" EXCLUDE)
install(FILES ${PROJECT_BINARY_DIR}/generated/gtosd/version.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gtosd)
install(DIRECTORY benchmarks/fixtures/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/gtosd/benchmarks)
install(EXPORT gtosdTargets
  FILE gtosdTargets.cmake
  NAMESPACE gtosd::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gtosd)
configure_package_config_file(
  cmake/gtosdConfig.cmake.in
  ${PROJECT_BINARY_DIR}/gtosdConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gtosd)
write_basic_package_version_file(
  ${PROJECT_BINARY_DIR}/gtosdConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
    ${PROJECT_BINARY_DIR}/gtosdConfig.cmake
    ${PROJECT_BINARY_DIR}/gtosdConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gtosd)
