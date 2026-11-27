include_guard(GLOBAL)

# The script host: awtrix-host pairs ESP32 clocks and runs one awtrix-script-runner for each,
# which runs that clock's scripts. Neither needs Linux.
include("${CMAKE_CURRENT_LIST_DIR}/WebUi.cmake")
set(HOSTD_DIR "${PROJECT_SOURCE_DIR}/src/platform/linux/hostd")
set(RUNNER_DIR "${PROJECT_SOURCE_DIR}/src/platform/linux")

add_library(awtrix_hmac STATIC "${PROJECT_SOURCE_DIR}/src/transport/net/HmacSha256.cpp")
target_link_libraries(awtrix_hmac PUBLIC awtrix_core PRIVATE OpenSSL::Crypto awtrix_warnings)

add_library(awtrix_hostd STATIC "${HOSTD_DIR}/ClockStore.cpp" "${HOSTD_DIR}/Children.cpp" "${HOSTD_DIR}/CodeAccess.cpp"
  "${HOSTD_DIR}/Discovery.cpp")
target_link_libraries(awtrix_hostd PUBLIC awtrix_core awtrix_json awtrix_os awtrix_hmac Threads::Threads
  PRIVATE awtrix_warnings)

add_executable(awtrix-host "${HOSTD_DIR}/main_hostd.cpp")
target_link_libraries(awtrix-host PRIVATE awtrix_hostd awtrix_warnings)

add_library(awtrix_remote STATIC "${RUNNER_DIR}/remote/ClockApi.cpp" "${RUNNER_DIR}/remote/RemoteClockApplication.cpp"
  "${RUNNER_DIR}/remote/RemoteRole.cpp" "${PROJECT_SOURCE_DIR}/src/transport/mqtt/ScriptOnlyMqtt.cpp")
target_link_libraries(awtrix_remote PUBLIC awtrix_host awtrix_hmac PRIVATE awtrix_warnings)

add_executable(awtrix-script-runner "${RUNNER_DIR}/remote/main_runner.cpp" "${RUNNER_DIR}/remote/RunnerOptions.cpp"
  "${RUNNER_DIR}/remote/ScriptRunner.cpp" "${RUNNER_DIR}/LinuxDeviceFacts.cpp" "${RUNNER_DIR}/LinuxScriptHttp.cpp")
add_dependencies(awtrix-script-runner awtrix-webui)
target_link_libraries(awtrix-script-runner PRIVATE awtrix_remote awtrix_tcp awtrix_oauth awtrix_secrets awtrix_images
  awtrix_warnings)
install(TARGETS awtrix-host awtrix-script-runner RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(WIN32)
  include("${CMAKE_CURRENT_LIST_DIR}/HostWindows.cmake")
else()
  configure_file("${PROJECT_SOURCE_DIR}/packaging/host/awtrix-host.service.in" awtrix-host.service @ONLY)
  install(FILES "${PROJECT_BINARY_DIR}/awtrix-host.service" DESTINATION ${CMAKE_INSTALL_DATADIR}/awtrix-ng/systemd)
endif()
