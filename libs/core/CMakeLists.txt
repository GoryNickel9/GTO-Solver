add_library(gtosd_core
  src/cards.cpp
  src/game.cpp
  src/money.cpp
  src/ranges.cpp)
add_library(gtosd::core ALIAS gtosd_core)

target_include_directories(gtosd_core
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_core PUBLIC cxx_std_20)
set_target_properties(gtosd_core PROPERTIES EXPORT_NAME core)
gtosd_set_warnings(gtosd_core)
