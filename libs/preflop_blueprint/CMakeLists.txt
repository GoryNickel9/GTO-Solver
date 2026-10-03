find_package(nlohmann_json CONFIG REQUIRED)

add_library(gtosd_preflop_blueprint
  src/compiled_game.cpp
  src/game_config.cpp
  src/game_model.cpp
  src/board_context.cpp
  src/board_texture.cpp
  src/class_bucket_rows.cpp
  src/decision_gap.cpp
  src/kernels.cpp
  src/multiway_kernels.cpp
  src/multiway_kernels_avx2.cpp
  src/traversal.cpp
  src/river_engine.cpp
  src/trainer.cpp
  src/preflop_class_cache.cpp
  src/best_response.cpp
  src/policy_file.cpp
  src/certifier.cpp
  src/action_labels.cpp
  src/policy_query.cpp
  src/chart_export.cpp
  src/comparator.cpp
  src/postflop_tree_export.cpp
  src/query_worker.cpp)
add_library(gtosd::preflop_blueprint ALIAS gtosd_preflop_blueprint)
# The AVX2 inner loops of the multiway kernels, selected at run time (multiway_kernels.hpp).
if(MSVC)
  set_source_files_properties(src/multiway_kernels_avx2.cpp PROPERTIES COMPILE_OPTIONS /arch:AVX2)
else()
  set_source_files_properties(src/multiway_kernels_avx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")
endif()
if(WIN32)
  # process_memory_peaks() uses GetProcessMemoryInfo.
  target_link_libraries(gtosd_preflop_blueprint PRIVATE psapi)
endif()

target_include_directories(gtosd_preflop_blueprint
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_preflop_blueprint PUBLIC cxx_std_20)
# Allowed chain (roadmap section 2.2): core, equity, tree, card_abstraction.
# The legacy preflop trainer and the standalone postflop solver must never
# appear here; tests/verify_preflop_blueprint_isolation.cmake enforces it.
target_link_libraries(gtosd_preflop_blueprint
  PUBLIC gtosd::core gtosd::equity gtosd::tree gtosd::card_abstraction
  PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(gtosd_preflop_blueprint PROPERTIES EXPORT_NAME preflop_blueprint)
gtosd_set_warnings(gtosd_preflop_blueprint)
