find_package(nlohmann_json CONFIG REQUIRED)

add_library(gtosd_preflop_trainer
  src/config.cpp
  src/hu_preflop.cpp
  src/hu_preflop_sampled_persistence.cpp
  src/hu_preflop_solver.cpp)
add_library(gtosd::preflop_trainer ALIAS gtosd_preflop_trainer)

target_include_directories(gtosd_preflop_trainer
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_preflop_trainer PUBLIC cxx_std_20)
target_link_libraries(gtosd_preflop_trainer
  PUBLIC gtosd::core gtosd::equity gtosd::tree
  PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(gtosd_preflop_trainer PROPERTIES EXPORT_NAME preflop_trainer)
gtosd_set_warnings(gtosd_preflop_trainer)

add_library(gtosd_preflop
  src/config.cpp
  src/hu_preflop.cpp
  src/hu_preflop_decomposition.cpp
  src/hu_preflop_legacy_bridge.cpp
  src/hu_preflop_nashconv.cpp
  src/hu_preflop_persistence.cpp
  src/hu_preflop_sampled_persistence.cpp
  src/hu_preflop_sampled_evaluation.cpp
  src/hu_preflop_solver.cpp)
add_library(gtosd::preflop ALIAS gtosd_preflop)

target_include_directories(gtosd_preflop
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_preflop PUBLIC cxx_std_20)
target_link_libraries(gtosd_preflop
  PUBLIC gtosd::core gtosd::equity gtosd::tree
  PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(gtosd_preflop PROPERTIES EXPORT_NAME preflop)
gtosd_set_warnings(gtosd_preflop)
