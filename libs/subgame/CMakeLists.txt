add_library(gtosd_subgame
  src/subgame_solver.cpp)
add_library(gtosd::subgame ALIAS gtosd_subgame)

target_include_directories(gtosd_subgame
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_subgame PUBLIC cxx_std_20)
target_link_libraries(gtosd_subgame PUBLIC gtosd::abstraction gtosd::best_response)
set_target_properties(gtosd_subgame PROPERTIES EXPORT_NAME subgame)
gtosd_set_warnings(gtosd_subgame)
