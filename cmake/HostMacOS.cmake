include_guard(GLOBAL)

enable_language(OBJCXX)
set(HOSTMAC_DIR "${PROJECT_SOURCE_DIR}/src/platform/host/app/macos")
add_library(awtrix_hostmac STATIC "${HOSTMAC_DIR}/StartAtLogin.cpp" "${HOSTMAC_DIR}/LaunchAgentLoginItem.cpp"
  "${HOSTMAC_DIR}/DefaultsChoice.mm" "${HOSTMAC_DIR}/DispatchHostProcess.mm" "${HOSTMAC_DIR}/WakeTimer.mm"
  "${HOSTMAC_DIR}/HostDriver.mm" "${HOSTMAC_DIR}/AppPaths.cpp" "${HOSTMAC_DIR}/SystemLoginItem.mm"
  "${HOSTMAC_DIR}/Text.mm" "${HOSTMAC_DIR}/StatusIcon.mm" "${HOSTMAC_DIR}/MenuFill.mm")
target_compile_options(awtrix_hostmac PUBLIC "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
target_link_libraries(awtrix_hostmac PUBLIC awtrix_hostapp awtrix_os "-framework AppKit"
  "-weak_framework ServiceManagement" PRIVATE awtrix_warnings)

string(REGEX MATCH "^[0-9]+(\\.[0-9]+)*" AWTRIX_BUNDLE_VERSION "${AWTRIX_VERSION}")
configure_file("${PROJECT_SOURCE_DIR}/packaging/host/macos/Info.plist.in" HostInfo.plist @ONLY)
set(HOSTMAC_ICON "${PROJECT_SOURCE_DIR}/tools/tc002/desktop/native/icons/icon.icns")
set_source_files_properties("${HOSTMAC_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
add_executable(awtrix-script-host MACOSX_BUNDLE "${HOSTMAC_DIR}/main.mm" "${HOSTMAC_DIR}/AppDelegate.mm"
  "${HOSTMAC_DIR}/StatusBar.mm" "${HOSTMAC_ICON}")
set_target_properties(awtrix-script-host PROPERTIES OUTPUT_NAME "AWTRIX Script Host"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/HostInfo.plist")
target_link_libraries(awtrix-script-host PRIVATE awtrix_hostmac awtrix_warnings)

set(HOSTMAC_CONTENTS "${CMAKE_CURRENT_BINARY_DIR}/AWTRIX Script Host.app/Contents")
set(HOSTMAC_PARTS "${HOSTMAC_CONTENTS}/MacOS/awtrix-host" "${HOSTMAC_CONTENTS}/MacOS/awtrix-script-runner"
  "${HOSTMAC_CONTENTS}/Resources/index.html")
add_custom_command(OUTPUT ${HOSTMAC_PARTS}
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${HOSTMAC_CONTENTS}/MacOS" "${HOSTMAC_CONTENTS}/Resources"
  COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:awtrix-host>" "$<TARGET_FILE:awtrix-script-runner>"
    "${HOSTMAC_CONTENTS}/MacOS/"
  COMMAND "${CMAKE_COMMAND}" -E copy "${WEBUI_HTML}" "${HOSTMAC_CONTENTS}/Resources/index.html"
  DEPENDS awtrix-host awtrix-script-runner awtrix-webui "${WEBUI_HTML}"
  VERBATIM)
add_custom_target(awtrix-script-host-parts ALL DEPENDS ${HOSTMAC_PARTS})
add_dependencies(awtrix-script-host awtrix-script-host-parts)
