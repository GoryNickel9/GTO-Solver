add_library(gtosd_card_abstraction
  src/all_in_table.cpp
  src/bucket_tables.cpp
  src/canonical_boards.cpp
  src/card_abstraction.cpp
  src/combinatorics.cpp
  src/exact_features.cpp
  src/rank_table.cpp
  src/resource_file.cpp
  src/showdown_counts.cpp)
add_library(gtosd::card_abstraction ALIAS gtosd_card_abstraction)

target_include_directories(gtosd_card_abstraction
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_card_abstraction PUBLIC cxx_std_20)
# Deliberately narrow dependency chain (roadmap P0-P10, section 2.2): cards,
# rules and the exact evaluator only. No preflop legacy, no postflop solver.
target_link_libraries(gtosd_card_abstraction
  PUBLIC gtosd::core gtosd::equity)
set_target_properties(gtosd_card_abstraction PROPERTIES EXPORT_NAME card_abstraction)
gtosd_set_warnings(gtosd_card_abstraction)
