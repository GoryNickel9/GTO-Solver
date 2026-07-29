find_package(imgui CONFIG REQUIRED)

add_executable(gto_gui_imgui_prototype WIN32
  main.cpp)
target_link_libraries(gto_gui_imgui_prototype
  PRIVATE
    gtosd::gui_prototype
    imgui::imgui
    d3d11
    dxgi
    shell32)
target_compile_definitions(gto_gui_imgui_prototype PRIVATE
  NOMINMAX
  UNICODE
  _UNICODE)
gtosd_set_warnings(gto_gui_imgui_prototype)

install(TARGETS gto_gui_imgui_prototype
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/imgui/copyright
  DESTINATION ${CMAKE_INSTALL_DATADIR}/gtosd/licenses/imgui
  RENAME LICENSE)

if(GTOSD_BUILD_TESTS)
  foreach(GTOSD_DPI_PERCENT IN ITEMS 100 150 200)
    add_test(NAME gtosd_phase9_imgui_e2e_${GTOSD_DPI_PERCENT}
      COMMAND gto_gui_imgui_prototype --e2e
        ${CMAKE_CURRENT_BINARY_DIR}/phase9_imgui_e2e_${GTOSD_DPI_PERCENT}.json)
    set_tests_properties(gtosd_phase9_imgui_e2e_${GTOSD_DPI_PERCENT}
      PROPERTIES
        LABELS "phase9;gui;imgui;e2e;d3d11;dpi"
        ENVIRONMENT "GTOSD_EXPECTED_DPI_PERCENT=${GTOSD_DPI_PERCENT}"
        ENVIRONMENT_MODIFICATION
          "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
  endforeach()
endif()
