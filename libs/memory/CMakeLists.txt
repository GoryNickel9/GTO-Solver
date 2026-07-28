add_library(gtosd_memory
  src/memory.cpp)
add_library(gtosd::memory ALIAS gtosd_memory)

target_include_directories(gtosd_memory
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_memory PUBLIC cxx_std_20)
target_link_libraries(gtosd_memory
  PUBLIC
    gtosd::tree
    gtosd::solver)
if(WIN32)
  target_link_libraries(gtosd_memory PRIVATE psapi)
endif()
set_target_properties(gtosd_memory PROPERTIES EXPORT_NAME memory)
gtosd_set_warnings(gtosd_memory)
