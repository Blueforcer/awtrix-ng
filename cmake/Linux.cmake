include_guard(GLOBAL)

# awtrix-linux: the headless Linux build and, with --board tc002, the TC002 runtime.
set(LINUX_DIR "${PROJECT_SOURCE_DIR}/src/platform/linux")
include(cmake/Tc002Voice.cmake)
include(cmake/ScriptExtensions.cmake)
include(cmake/WebUi.cmake)

add_library(tc002_audio_script STATIC "${PROJECT_SOURCE_DIR}/src/platform/tc002/audio/AudioScripting.cpp")
target_link_libraries(tc002_audio_script PUBLIC awtrix_script_extensions PRIVATE awtrix_warnings)

add_library(awtrix_layout STATIC "${LINUX_DIR}/layout/Layout.cpp" "${LINUX_DIR}/layout/LayoutJson.cpp"
  "${LINUX_DIR}/layout/LayoutPayload.cpp" "${LINUX_DIR}/layout/ScriptLayouts.cpp"
  "${LINUX_DIR}/layout/ScriptLayoutBindings.cpp" "${LINUX_DIR}/layout/LayoutScripting.cpp")
target_link_libraries(awtrix_layout PUBLIC awtrix_script_extensions PRIVATE awtrix_warnings)

# Bluetooth LE for scripts: the protocol pieces and the hub that shares one controller.
add_library(awtrix_ble STATIC "${LINUX_DIR}/ble/AdvData.cpp" "${LINUX_DIR}/ble/Att.cpp" "${LINUX_DIR}/ble/BleHub.cpp"
  "${LINUX_DIR}/ble/BleHubLinks.cpp" "${LINUX_DIR}/ble/BleHubGatt.cpp"
  "${LINUX_DIR}/ble/BleHubPeripheral.cpp" "${LINUX_DIR}/ble/BleScanner.cpp" "${LINUX_DIR}/ble/HubJson.cpp"
  "${LINUX_DIR}/ble/BleTypes.cpp" "${LINUX_DIR}/ble/GattClient.cpp" "${LINUX_DIR}/ble/GattServer.cpp"
  "${LINUX_DIR}/ble/LinuxBleRadio.cpp"
  "${LINUX_DIR}/ble/LinuxBleRadioMgmt.cpp" "${LINUX_DIR}/ble/LinuxBleRadioHci.cpp"
  "${LINUX_DIR}/ble/LinuxBleRadioLinks.cpp" "${LINUX_DIR}/ble/MgmtSocket.cpp"
  "${LINUX_DIR}/ble/BondStore.cpp" "${LINUX_DIR}/ble/BleService.cpp" "${LINUX_DIR}/ble/Gamepad.cpp"
  "${LINUX_DIR}/ble/GamepadManager.cpp"
  "${LINUX_DIR}/ble/HidGamepad.cpp" "${LINUX_DIR}/ble/Ancs.cpp" "${LINUX_DIR}/ble/Ams.cpp"
  "${LINUX_DIR}/ble/IphoneLink.cpp")
target_link_libraries(awtrix_ble PUBLIC awtrix_json awtrix_posix Threads::Threads PRIVATE awtrix_warnings)

# Bluetooth script modules and controller HTTP routes.
add_library(awtrix_ble_script STATIC "${LINUX_DIR}/ble/BleScripting.cpp" "${LINUX_DIR}/ble/GamepadScripting.cpp"
  "${LINUX_DIR}/ble/GamepadApi.cpp" "${LINUX_DIR}/ble/RemoteGamepad.cpp")
target_link_libraries(awtrix_ble_script PUBLIC awtrix_ble awtrix_script_extensions PRIVATE awtrix_warnings)

add_library(awtrix_script_ext STATIC "${LINUX_DIR}/script/CryptoScripting.cpp"
  "${LINUX_DIR}/script/TcpClients.cpp" "${LINUX_DIR}/script/TcpScripting.cpp"
  "${LINUX_DIR}/script/PowScanner.cpp" "${LINUX_DIR}/script/CryptoMiner.cpp" "${LINUX_DIR}/script/Sha256d.cpp")
target_link_libraries(awtrix_script_ext PUBLIC awtrix_script_extensions awtrix_posix Threads::Threads PRIVATE OpenSSL::Crypto awtrix_warnings)
# The mining kernel is built for speed, also in the -Os builds of the TC002.
set_source_files_properties("${LINUX_DIR}/script/Sha256d.cpp" PROPERTIES COMPILE_OPTIONS -O2)

# Sign-in for scripts: the @oauth line, its private records, token handling and HTTP routes.
add_library(awtrix_oauth STATIC "${LINUX_DIR}/oauth/OAuthSpec.cpp" "${LINUX_DIR}/oauth/OAuthFlow.cpp"
  "${LINUX_DIR}/oauth/OAuthVault.cpp" "${LINUX_DIR}/oauth/OAuthService.cpp" "${LINUX_DIR}/oauth/OAuthTransport.cpp"
  "${LINUX_DIR}/oauth/OAuthApi.cpp" "${LINUX_DIR}/oauth/OAuthScripting.cpp")
target_link_libraries(awtrix_oauth PUBLIC awtrix_script_extensions awtrix_posix awtrix_host Threads::Threads PRIVATE OpenSSL::Crypto awtrix_warnings)

# Pictures from URLs: fetched, fitted and kept for every icon, script and layout that names one.
find_package(JPEG REQUIRED)
add_library(awtrix_images STATIC "${LINUX_DIR}/images/HttpPictureLoader.cpp" "${LINUX_DIR}/images/PictureDecoder.cpp"
  "${LINUX_DIR}/images/Inflate.cpp" "${LINUX_DIR}/images/JpegPicture.cpp" "${LINUX_DIR}/images/PictureFit.cpp"
  "${LINUX_DIR}/images/PngPicture.cpp" "${LINUX_DIR}/images/RemoteImageStore.cpp")
target_link_libraries(awtrix_images PUBLIC awtrix_host PRIVATE JPEG::JPEG awtrix_warnings)

# What an iPhone's link puts on the display, its settings and its HTTP routes.
add_library(awtrix_iphone STATIC "${LINUX_DIR}/iphone/IphoneBridge.cpp" "${LINUX_DIR}/iphone/IphoneCovers.cpp"
  "${LINUX_DIR}/iphone/IphonePayload.cpp" "${LINUX_DIR}/iphone/IphoneSettings.cpp")
target_link_libraries(awtrix_iphone PUBLIC awtrix_ble awtrix_host PRIVATE awtrix_warnings)

add_executable(awtrix-linux "${LINUX_DIR}/main_linux.cpp" "${LINUX_DIR}/LinuxOptions.cpp"
  "${LINUX_DIR}/LinuxRuntime.cpp" "${LINUX_DIR}/LinuxRuntimeLoop.cpp" "${TC002_RUNTIME_DIR}/Tc002KnobRouter.cpp"
  "${LINUX_DIR}/LinuxAdminSecurity.cpp" "${LINUX_DIR}/LinuxDeviceFacts.cpp"
  "${LINUX_DIR}/LinuxLanLogin.cpp" "${LINUX_DIR}/LinuxMqttTls.cpp"
  "${LINUX_DIR}/LinuxPerformanceReport.cpp" "${LINUX_DIR}/LinuxScriptHttp.cpp" "${LINUX_DIR}/LinuxButtonWebhook.cpp"
  "${LINUX_DIR}/iphone/LinuxCoverFetch.cpp"
  "${PROJECT_SOURCE_DIR}/src/transport/net/DiscoveryService.cpp"
  "${PROJECT_SOURCE_DIR}/src/transport/net/MirrorLink.cpp"
  ${TC002_RUNTIME_SOURCES} ${TC002_BOOT_SCREEN_SOURCES})
add_dependencies(awtrix-linux awtrix-webui)
target_link_libraries(awtrix-linux PRIVATE tc002_audio_script awtrix_layout awtrix_host awtrix_images awtrix_script_ext awtrix_oauth awtrix_ble awtrix_ble_script awtrix_iphone tc002_contract tc002_update_verify awtrix_update_state tc002_voice tc002_audio tc002_speech_voice awtrix_warnings)
if(TARGET tc002_mcu_prepare)
  target_link_libraries(awtrix-linux PRIVATE tc002_mcu_prepare)
  target_compile_definitions(awtrix-linux PRIVATE AWTRIX_MCU_PREPARE)
endif()

install(TARGETS awtrix-linux RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES "${PROJECT_SOURCE_DIR}/webui/index.html" DESTINATION ${CMAKE_INSTALL_DATADIR}/awtrix-ng)
configure_file("${PROJECT_SOURCE_DIR}/packaging/linux/awtrix-ng.service.in" awtrix-ng.service @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/awtrix-ng.service" DESTINATION ${CMAKE_INSTALL_DATADIR}/awtrix-ng/systemd)
