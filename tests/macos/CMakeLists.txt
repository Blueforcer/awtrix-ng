add_awtrix_test(NAME macos-login SOURCES test_login.mm LIBRARIES awtrix_hostmac)
add_awtrix_test(NAME macos-host-driver SOURCES test_host_driver.mm LIBRARIES awtrix_hostmac
  COMMAND "$<TARGET_FILE:awtrix-host>" "$<TARGET_FILE:awtrix-script-runner>" "${AWTRIX_WEBUI}" TIMEOUT 120
  PROPERTIES RUN_SERIAL TRUE)
add_awtrix_test(NAME macos-menu-bar SOURCES test_menu_bar.mm LIBRARIES awtrix_hostmac)
string(REPLACE ";" "," architectures "${CMAKE_OSX_ARCHITECTURES}")
add_awtrix_test(NAME macos-app-bundle SOURCES test_app_bundle.mm LIBRARIES awtrix_hostmac
  COMMAND "$<TARGET_BUNDLE_DIR:awtrix-script-host>" "${AWTRIX_BUNDLE_VERSION}" "${architectures}")
