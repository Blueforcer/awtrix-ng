include_guard(GLOBAL)

# awtrix-host: pairs ESP32 clocks and runs one awtrix-linux --remote process for each.
set(HOSTD_DIR "${PROJECT_SOURCE_DIR}/src/platform/linux/hostd")
add_library(awtrix_hostd STATIC "${HOSTD_DIR}/ClockStore.cpp" "${HOSTD_DIR}/Children.cpp" "${HOSTD_DIR}/Discovery.cpp"
  "${PROJECT_SOURCE_DIR}/src/transport/net/HmacSha256.cpp")
target_link_libraries(awtrix_hostd PUBLIC awtrix_core awtrix_json awtrix_posix Threads::Threads
  PRIVATE OpenSSL::Crypto awtrix_warnings)

add_executable(awtrix-host "${HOSTD_DIR}/main_hostd.cpp")
target_link_libraries(awtrix-host PRIVATE awtrix_hostd awtrix_warnings)
install(TARGETS awtrix-host RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
configure_file("${PROJECT_SOURCE_DIR}/packaging/host/awtrix-host.service.in" awtrix-host.service @ONLY)
install(FILES "${PROJECT_BINARY_DIR}/awtrix-host.service" DESTINATION ${CMAKE_INSTALL_DATADIR}/awtrix-ng/systemd)
