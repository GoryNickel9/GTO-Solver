add_library(gtosd_best_response
  src/best_response.cpp
  src/enumerated_best_response.cpp)
add_library(gtosd::best_response ALIAS gtosd_best_response)

target_include_directories(gtosd_best_response
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_best_response PUBLIC cxx_std_20)
target_link_libraries(gtosd_best_response PUBLIC gtosd::solver)
set_target_properties(gtosd_best_response PROPERTIES EXPORT_NAME best_response)
gtosd_set_warnings(gtosd_best_response)
