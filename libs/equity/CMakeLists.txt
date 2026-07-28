add_library(gtosd_equity
  src/evaluator.cpp
  src/showdown.cpp)
add_library(gtosd::equity ALIAS gtosd_equity)

target_include_directories(gtosd_equity
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_equity PUBLIC cxx_std_20)
target_link_libraries(gtosd_equity PUBLIC gtosd::core)
set_target_properties(gtosd_equity PROPERTIES EXPORT_NAME equity)
gtosd_set_warnings(gtosd_equity)
