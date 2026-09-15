add_library(gtosd_solver_validation
  src/tre_validation.cpp)
add_library(gtosd::solver_validation ALIAS gtosd_solver_validation)

target_include_directories(gtosd_solver_validation
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_solver_validation PUBLIC cxx_std_20)
target_link_libraries(gtosd_solver_validation PUBLIC gtosd::best_response)
set_target_properties(gtosd_solver_validation PROPERTIES EXPORT_NAME solver_validation)
gtosd_set_warnings(gtosd_solver_validation)
