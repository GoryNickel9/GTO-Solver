add_library(gtosd_storage
  src/storage.cpp)
add_library(gtosd::storage ALIAS gtosd_storage)

find_package(unofficial-sodium CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)

target_include_directories(gtosd_storage
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>)
target_compile_features(gtosd_storage PUBLIC cxx_std_20)
target_link_libraries(gtosd_storage
  PUBLIC
    gtosd::postflop
  PRIVATE
    unofficial-sodium::sodium
    unofficial::sqlite3::sqlite3
    zstd::libzstd)
set_target_properties(gtosd_storage PROPERTIES EXPORT_NAME storage)
gtosd_set_warnings(gtosd_storage)

if(WIN32)
  install(FILES
      ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/libsodium.dll
      ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/sqlite3.dll
      ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/zstd.dll
    DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(FILES
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/libsodium/copyright
  DESTINATION ${CMAKE_INSTALL_DATADIR}/gtosd/licenses/libsodium
  RENAME LICENSE)
install(FILES
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/sqlite3/copyright
  DESTINATION ${CMAKE_INSTALL_DATADIR}/gtosd/licenses/sqlite3
  RENAME LICENSE)
install(FILES
    ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/zstd/copyright
  DESTINATION ${CMAKE_INSTALL_DATADIR}/gtosd/licenses/zstd
  RENAME LICENSE)
