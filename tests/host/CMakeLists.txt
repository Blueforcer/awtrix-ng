cmake_minimum_required(VERSION 3.20)
get_filename_component(AWTRIX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(awtrix_host_tests LANGUAGES C CXX)
  include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ProjectOptions.cmake")
  set(CMAKE_CXX_EXTENSIONS OFF)
  include(CTest)
  include("${AWTRIX_ROOT}/cmake/Warnings.cmake")
  include("${AWTRIX_ROOT}/cmake/Testing.cmake")
endif()
include("${AWTRIX_ROOT}/cmake/Posix.cmake")
find_package(Threads REQUIRED)

set(host "${AWTRIX_ROOT}/src/platform/linux/host")
set(host_test INCLUDES "${AWTRIX_ROOT}/src" "${host}/compat" DEFINITIONS AWTRIX_NATIVE SANITIZE)
add_awtrix_test(NAME host-store SOURCES test_store.cpp "${host}/HostStore.cpp"
  "${AWTRIX_ROOT}/src/media/AssetFileDevice.cpp" LIBRARIES Threads::Threads awtrix_posix
  ${host_test})
add_awtrix_test(NAME host-resolver SOURCES test_resolver.cpp "${host}/HostResolver.cpp"
  LIBRARIES Threads::Threads ${host_test})
add_awtrix_test(NAME host-persistence SOURCES test_persistence.cpp "${host}/HostStore.cpp"
  "${host}/HostPendingWrites.cpp" LIBRARIES Threads::Threads awtrix_posix ${host_test})
if(TARGET awtrix_core)
  add_awtrix_test(NAME host-buttons SOURCES test_host_buttons.cpp "${host}/HostButtonInput.cpp"
    LIBRARIES awtrix_core)
endif()

if(TARGET awtrix_host)
  add_awtrix_test(NAME host-mqtt-link SOURCES test_mqtt_link.cpp LIBRARIES awtrix_host SANITIZE)
  add_awtrix_test(NAME host-tcp-client SOURCES test_tcp_client.cpp LIBRARIES awtrix_host SANITIZE)
  add_awtrix_test(NAME host-log SOURCES test_log.cpp LIBRARIES awtrix_host SANITIZE)
  target_link_options(host-log-test PRIVATE -Wl,--wrap=time)
endif()
