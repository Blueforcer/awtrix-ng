include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/Posix.cmake")
set(AWTRIX_OS_DIR "${CMAKE_CURRENT_LIST_DIR}/../src/platform/host")
add_library(awtrix_os STATIC "${AWTRIX_OS_DIR}/Memory.cpp" "${AWTRIX_OS_DIR}/PrivateStore.cpp"
  "${AWTRIX_OS_DIR}/Resolver.cpp" "${AWTRIX_OS_DIR}/ZoneRule.cpp")
target_link_libraries(awtrix_os PUBLIC awtrix_portable PRIVATE awtrix_warnings)
if(UNIX)
  target_sources(awtrix_os PRIVATE "${AWTRIX_OS_DIR}/posix/Files.cpp" "${AWTRIX_OS_DIR}/posix/Memory.cpp"
    "${AWTRIX_OS_DIR}/posix/Process.cpp" "${AWTRIX_OS_DIR}/posix/Log.cpp" "${AWTRIX_OS_DIR}/posix/Socket.cpp"
    "${AWTRIX_OS_DIR}/posix/System.cpp" "${AWTRIX_OS_DIR}/posix/Timer.cpp" "${AWTRIX_OS_DIR}/posix/Wake.cpp")
  target_link_libraries(awtrix_os PRIVATE awtrix_posix)
elseif(WIN32)
  target_sources(awtrix_os PRIVATE "${AWTRIX_OS_DIR}/win32/Files.cpp" "${AWTRIX_OS_DIR}/win32/Memory.cpp"
    "${AWTRIX_OS_DIR}/win32/Process.cpp" "${AWTRIX_OS_DIR}/win32/Log.cpp" "${AWTRIX_OS_DIR}/win32/Socket.cpp"
    "${AWTRIX_OS_DIR}/win32/System.cpp" "${AWTRIX_OS_DIR}/win32/Timer.cpp" "${AWTRIX_OS_DIR}/win32/Wake.cpp")
  target_compile_definitions(awtrix_os PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX NOGDI _WIN32_WINNT=0x0A00)
  target_compile_options(awtrix_os PUBLIC "$<$<COMPILE_LANGUAGE:C,CXX>:--include=${AWTRIX_OS_DIR}/win32/Windows.h>")
  target_link_libraries(awtrix_os PUBLIC ws2_32 iphlpapi advapi32 winmm)
endif()
