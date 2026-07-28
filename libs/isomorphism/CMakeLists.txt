add_library(gtosd_isomorphism
  src/isomorphism.cpp)
add_library(gtosd::isomorphism ALIAS gtosd_isomorphism)

target_include_directories(gtosd_isomorphism
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_isomorphism PUBLIC cxx_std_20)
target_link_libraries(gtosd_isomorphism PUBLIC gtosd::tree)
set_target_properties(gtosd_isomorphism PROPERTIES EXPORT_NAME isomorphism)
gtosd_set_warnings(gtosd_isomorphism)
