find_package(nlohmann_json CONFIG REQUIRED)

add_library(gtosd_tree
  src/config.cpp
  src/tree.cpp)
add_library(gtosd::tree ALIAS gtosd_tree)

target_include_directories(gtosd_tree
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_tree PUBLIC cxx_std_20)
target_link_libraries(gtosd_tree
  PUBLIC
    gtosd::core
    gtosd::equity
  PRIVATE
    nlohmann_json::nlohmann_json)
set_target_properties(gtosd_tree PROPERTIES EXPORT_NAME tree)
gtosd_set_warnings(gtosd_tree)
