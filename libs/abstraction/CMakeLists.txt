add_library(gtosd_abstraction
  src/card_abstraction.cpp)
add_library(gtosd::abstraction ALIAS gtosd_abstraction)

target_include_directories(gtosd_abstraction
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_abstraction PUBLIC cxx_std_20)
target_link_libraries(gtosd_abstraction PUBLIC gtosd::solver gtosd::equity)
set_target_properties(gtosd_abstraction PROPERTIES EXPORT_NAME abstraction)
gtosd_set_warnings(gtosd_abstraction)
