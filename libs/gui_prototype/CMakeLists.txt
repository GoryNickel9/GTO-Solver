add_library(gtosd_gui_prototype
  src/gui_prototype.cpp)
add_library(gtosd::gui_prototype ALIAS gtosd_gui_prototype)

target_include_directories(gtosd_gui_prototype
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_gui_prototype PUBLIC cxx_std_20)
target_link_libraries(gtosd_gui_prototype
  PUBLIC
    gtosd::storage)
if(WIN32)
  target_link_libraries(gtosd_gui_prototype PRIVATE advapi32 d3d11 dxgi)
endif()
set_target_properties(gtosd_gui_prototype PROPERTIES EXPORT_NAME gui_prototype)
gtosd_set_warnings(gtosd_gui_prototype)
