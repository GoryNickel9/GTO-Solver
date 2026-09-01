add_library(gtosd_postflop
  src/canonical_layout.cpp
  src/postflop_solver.cpp)
add_library(gtosd::postflop ALIAS gtosd_postflop)

target_include_directories(gtosd_postflop
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_postflop PUBLIC cxx_std_20)
target_link_libraries(gtosd_postflop
  PUBLIC
    gtosd::memory
    gtosd::equity
    gtosd::isomorphism)
set_target_properties(gtosd_postflop PROPERTIES EXPORT_NAME postflop)
gtosd_set_warnings(gtosd_postflop)

option(GTOSD_ENABLE_HOTPATH_PROFILE
  "Compile detailed postflop hot-path instrumentation" OFF)
if(GTOSD_ENABLE_HOTPATH_PROFILE)
  target_compile_definitions(gtosd_postflop PRIVATE GTOSD_ENABLE_HOTPATH_PROFILE=1)
endif()

option(GTOSD_ENABLE_REAL_NODE_REPLAY
  "Compile bounded diagnostic real-node replay capture" OFF)
if(GTOSD_ENABLE_REAL_NODE_REPLAY)
  target_compile_definitions(gtosd_postflop PRIVATE GTOSD_ENABLE_REAL_NODE_REPLAY=1)
endif()

if(GTOSD_BUILD_RANGE_ORBIT_ORACLE)
  add_library(gtosd_postflop_range_orbit_oracle
    src/canonical_layout.cpp
    src/postflop_solver.cpp)
  target_include_directories(gtosd_postflop_range_orbit_oracle
    PUBLIC
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>)
  target_compile_features(gtosd_postflop_range_orbit_oracle PUBLIC cxx_std_20)
  target_compile_definitions(gtosd_postflop_range_orbit_oracle
    PRIVATE GTOSD_ENABLE_RANGE_ORBIT_ORACLE=1)
  target_link_libraries(gtosd_postflop_range_orbit_oracle
    PUBLIC
      gtosd::memory
      gtosd::equity
      gtosd::isomorphism)
  gtosd_set_warnings(gtosd_postflop_range_orbit_oracle)
endif()

# The desktop performance target is x86-64 with AVX2. Keep strict IEEE
# semantics (no /fp:fast), while enabling whole-program optimization and the
# native vector instruction set for the exact postflop hot path.
if(MSVC)
  target_compile_options(gtosd_postflop PRIVATE
    $<$<CONFIG:Release>:/Ob3 /Oi /Ot /GL /arch:AVX2 /favor:INTEL64>)
  target_link_options(gtosd_postflop INTERFACE
    $<$<CONFIG:Release>:/LTCG>)
  if(GTOSD_BUILD_RANGE_ORBIT_ORACLE)
    target_compile_options(gtosd_postflop_range_orbit_oracle PRIVATE
      $<$<CONFIG:Release>:/Ob3 /Oi /Ot /GL /arch:AVX2 /favor:INTEL64>)
    target_link_options(gtosd_postflop_range_orbit_oracle INTERFACE
      $<$<CONFIG:Release>:/LTCG>)
  endif()
endif()
