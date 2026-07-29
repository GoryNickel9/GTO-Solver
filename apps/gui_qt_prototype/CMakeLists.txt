find_package(Qt6 CONFIG REQUIRED COMPONENTS Test Widgets)

add_library(gtosd_qt_prototype_ui
  prototype_window.cpp
  prototype_window.hpp)
target_link_libraries(gtosd_qt_prototype_ui
  PUBLIC
    gtosd::gui_prototype
    Qt6::Widgets)
target_compile_features(gtosd_qt_prototype_ui PUBLIC cxx_std_20)
gtosd_set_warnings(gtosd_qt_prototype_ui)

add_executable(gto_gui_qt_prototype
  main.cpp)
target_link_libraries(gto_gui_qt_prototype
  PRIVATE
    gtosd_qt_prototype_ui
    Qt6::Test)
gtosd_set_warnings(gto_gui_qt_prototype)

install(TARGETS gto_gui_qt_prototype
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
qt_generate_deploy_app_script(
  TARGET gto_gui_qt_prototype
  OUTPUT_SCRIPT GTOSD_QT_DEPLOY_SCRIPT
  NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${GTOSD_QT_DEPLOY_SCRIPT})
install(FILES
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/double-conversion.dll
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/md4c.dll
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/pcre2-16.dll
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/z.dll
  DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms/qminimal.dll
  DESTINATION Qt6/plugins/platforms)
install(FILES
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/qtbase/copyright
  DESTINATION ${CMAKE_INSTALL_DATADIR}/gtosd/licenses/qtbase
  RENAME LICENSE)
foreach(GTOSD_QT_RUNTIME_DEPENDENCY IN ITEMS double-conversion md4c pcre2 zlib)
  install(FILES
      ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${GTOSD_QT_RUNTIME_DEPENDENCY}/copyright
    DESTINATION
      ${CMAKE_INSTALL_DATADIR}/gtosd/licenses/${GTOSD_QT_RUNTIME_DEPENDENCY}
    RENAME LICENSE)
endforeach()

if(GTOSD_BUILD_TESTS)
  foreach(GTOSD_DPI_PERCENT IN ITEMS 100 150 200)
    math(EXPR GTOSD_DPI_WHOLE "${GTOSD_DPI_PERCENT} / 100")
    math(EXPR GTOSD_DPI_REMAINDER "${GTOSD_DPI_PERCENT} % 100")
    if(GTOSD_DPI_REMAINDER EQUAL 0)
      set(GTOSD_QT_SCALE_FACTOR "${GTOSD_DPI_WHOLE}.0")
    else()
      set(GTOSD_QT_SCALE_FACTOR "${GTOSD_DPI_WHOLE}.5")
    endif()
    add_test(NAME gtosd_phase9_qt_e2e_${GTOSD_DPI_PERCENT}
      COMMAND gto_gui_qt_prototype --e2e
        ${CMAKE_CURRENT_BINARY_DIR}/phase9_qt_e2e_${GTOSD_DPI_PERCENT}.json)
    set_tests_properties(gtosd_phase9_qt_e2e_${GTOSD_DPI_PERCENT}
      PROPERTIES
        LABELS "phase9;gui;qt;e2e;accessibility;dpi"
        ENVIRONMENT
          "QT_QPA_PLATFORM=minimal;QT_SCALE_FACTOR=${GTOSD_QT_SCALE_FACTOR};GTOSD_EXPECTED_DPI_PERCENT=${GTOSD_DPI_PERCENT}"
        ENVIRONMENT_MODIFICATION
          "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin;QT_PLUGIN_PATH=set:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins")
  endforeach()
endif()
