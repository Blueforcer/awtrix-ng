cmake_minimum_required(VERSION 3.20)
get_filename_component(AWTRIX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(awtrix_host_tests LANGUAGES C CXX)
  include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProjectOptions.cmake")
  set(CMAKE_CXX_EXTENSIONS OFF)
  include(CTest)
  include("${AWTRIX_ROOT}/cmake/Warnings.cmake")
  include("${AWTRIX_ROOT}/cmake/Sanitizers.cmake")
  include("${AWTRIX_ROOT}/cmake/Testing.cmake")
endif()
include("${AWTRIX_ROOT}/cmake/Posix.cmake")
include("${AWTRIX_ROOT}/cmake/Os.cmake")
find_package(Threads REQUIRED)

set(host "${AWTRIX_ROOT}/src/platform/linux/host")
set(host_test INCLUDES "${AWTRIX_ROOT}/src" "${host}/compat" DEFINITIONS AWTRIX_NATIVE)
add_awtrix_test(NAME host-store SOURCES test_store.cpp "${host}/HostStore.cpp"
  "${AWTRIX_ROOT}/src/media/AssetFileDevice.cpp" LIBRARIES Threads::Threads awtrix_os
  $<TARGET_NAME_IF_EXISTS:awtrix_posix> ${host_test})
add_awtrix_test(NAME host-resolver SOURCES test_resolver.cpp "${host}/HostResolver.cpp"
  LIBRARIES Threads::Threads awtrix_os ${host_test})
add_awtrix_test(NAME host-persistence SOURCES test_persistence.cpp "${host}/HostStore.cpp"
  "${host}/HostPendingWrites.cpp" LIBRARIES Threads::Threads awtrix_os $<TARGET_NAME_IF_EXISTS:awtrix_posix>
  ${host_test})
add_awtrix_test(NAME host-zone-rule SOURCES test_zone_rule.cpp "${AWTRIX_ROOT}/src/platform/host/ZoneRule.cpp"
  INCLUDES "${AWTRIX_ROOT}/src" COMMAND "${AWTRIX_ROOT}/webui/src/generated/timezones.js")
add_awtrix_test(NAME host-timer SOURCES test_timer.cpp LIBRARIES awtrix_os
  INCLUDES "${AWTRIX_ROOT}/src" "${AWTRIX_ROOT}/src/platform/linux/include")
add_awtrix_test(NAME host-udp-port SOURCES test_udp_port.cpp "${AWTRIX_ROOT}/src/transport/net/UdpSocket.cpp"
  "${AWTRIX_ROOT}/src/transport/net/UdpPort.cpp" LIBRARIES awtrix_os ${host_test})
if(TARGET awtrix_core)
  add_awtrix_test(NAME host-buttons SOURCES test_host_buttons.cpp "${host}/HostButtonInput.cpp"
    LIBRARIES awtrix_core)
endif()

if(TARGET awtrix_host)
  add_awtrix_test(NAME host-mqtt-link SOURCES test_mqtt_link.cpp LIBRARIES awtrix_host)
  if(NOT WIN32)
    add_awtrix_test(NAME host-tcp-client SOURCES test_tcp_client.cpp LIBRARIES awtrix_host)
    add_awtrix_test(NAME host-log SOURCES test_log.cpp LIBRARIES awtrix_host)
    if(NOT APPLE)
      target_link_options(host-log-test PRIVATE -Wl,--wrap=time)
    endif()
  endif()
endif()
if(WIN32)
  add_awtrix_test(NAME host-win32 SOURCES test_win32.cpp LIBRARIES awtrix_os
    ${host_test} TIMEOUT 60)
endif()
if(TARGET awtrix_tray)
  add_awtrix_test(NAME hostapp-win32 SOURCES test_hostapp_win32.cpp LIBRARIES awtrix_tray)
  add_awtrix_test(NAME hostapp-win32-process SOURCES test_hostapp_win32_process.cpp
    LIBRARIES awtrix_tray awtrix_tray_host awtrix_os
    COMMAND "$<TARGET_FILE:awtrix-host>" "$<TARGET_FILE:awtrix-script-runner>" "${AWTRIX_WEBUI}" TIMEOUT 120
    PROPERTIES RUN_SERIAL TRUE)
endif()
