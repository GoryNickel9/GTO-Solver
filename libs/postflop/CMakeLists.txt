add_library(gtosd_postflop
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
