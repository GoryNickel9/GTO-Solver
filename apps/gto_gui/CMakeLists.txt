find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets)

add_executable(gto_gui WIN32
  main.cpp
  product_window.cpp
  product_window.hpp)
target_link_libraries(gto_gui
  PRIVATE
    gtosd::gui_prototype
    gtosd::memory
    gtosd::postflop
    gtosd::storage
    Qt6::Widgets)
target_compile_features(gto_gui PRIVATE cxx_std_20)
gtosd_set_warnings(gto_gui)

install(TARGETS gto_gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
qt_generate_deploy_app_script(
  TARGET gto_gui
  OUTPUT_SCRIPT GTOSD_PRODUCT_QT_DEPLOY_SCRIPT
  NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${GTOSD_PRODUCT_QT_DEPLOY_SCRIPT})
install(FILES
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/double-conversion.dll
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/md4c.dll
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/pcre2-16.dll
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/z.dll
  DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins/platforms/qminimal.dll
  DESTINATION Qt6/plugins/platforms)
install(FILES translations/gtosd_en.ts
  DESTINATION ${CMAKE_INSTALL_DATADIR}/gtosd/translations)

if(GTOSD_BUILD_TESTS)
  add_test(NAME gtosd_phase10_product_e2e
    COMMAND gto_gui --e2e ${CMAKE_CURRENT_BINARY_DIR}/phase10_e2e.json)
  set_tests_properties(gtosd_phase10_product_e2e
    PROPERTIES
      LABELS "phase10;gui;qt;e2e;product"
      TIMEOUT 120
      ENVIRONMENT "QT_QPA_PLATFORM=minimal"
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin;QT_PLUGIN_PATH=set:${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/Qt6/plugins")
endif()
