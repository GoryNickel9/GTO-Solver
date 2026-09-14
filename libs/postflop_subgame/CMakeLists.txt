add_library(gtosd_postflop_subgame
  src/postflop_subgame.cpp)
add_library(gtosd::postflop_subgame ALIAS gtosd_postflop_subgame)

target_include_directories(gtosd_postflop_subgame
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_postflop_subgame PUBLIC cxx_std_20)
target_link_libraries(gtosd_postflop_subgame
  PUBLIC
    gtosd::postflop
    gtosd::solver)
set_target_properties(gtosd_postflop_subgame PROPERTIES EXPORT_NAME postflop_subgame)
gtosd_set_warnings(gtosd_postflop_subgame)
