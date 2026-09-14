add_library(gtosd_solver
  src/card_abstraction.cpp
  src/finite_game.cpp
  src/reference_games.cpp
  src/subgame.cpp
  src/solver.cpp)
add_library(gtosd::solver ALIAS gtosd_solver)

target_include_directories(gtosd_solver
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_solver PUBLIC cxx_std_20)
target_link_libraries(gtosd_solver
  PUBLIC
    gtosd::core
    gtosd::equity)
set_target_properties(gtosd_solver PROPERTIES EXPORT_NAME solver)
gtosd_set_warnings(gtosd_solver)
