include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/Tc002Flash.cmake")

# libawtrix-loader.so, the plugin the stock zkgui loads from res (tc002-loader preset). It runs in the
# stock userspace: the glibc toolchain builds it, and after every link Tc002LoaderCheck.cmake checks
# that it exports only zkgui's three entry points and needs no more than the stock glibc 2.30.
set(TC002_LOADER_DIR "${PROJECT_SOURCE_DIR}/src/platform/tc002/loader")
set(TC002_LOADER_SOURCES "${TC002_LOADER_DIR}/loader.c" "${TC002_LOADER_DIR}/loader_policy.c"
  "${TC002_LOADER_DIR}/loader_state.c" "${TC002_LOADER_DIR}/rescue_panel.c"
  ${TC002_FLASH_C_SOURCES})
add_library(awtrix-loader MODULE ${TC002_LOADER_SOURCES})
set_target_properties(awtrix-loader PROPERTIES C_VISIBILITY_PRESET hidden C_EXTENSIONS OFF)
target_compile_definitions(awtrix-loader PRIVATE _GNU_SOURCE)
target_link_options(awtrix-loader PRIVATE -static-libgcc -Wl,-soname,libawtrix-loader.so -Wl,-z,defs
  -Wl,-z,relro -Wl,-z,now -Wl,--gc-sections -Wl,--hash-style=both -Wl,--build-id=sha1)
target_link_libraries(awtrix-loader PRIVATE ${CMAKE_DL_LIBS} awtrix_warnings)
add_custom_command(TARGET awtrix-loader POST_BUILD
  COMMAND "${CMAKE_COMMAND}" "-DREADELF=${CMAKE_READELF}" "-DLOADER=$<TARGET_FILE:awtrix-loader>"
          -P "${CMAKE_CURRENT_LIST_DIR}/Tc002LoaderCheck.cmake"
  VERBATIM)
install(TARGETS awtrix-loader LIBRARY DESTINATION lib COMPONENT tc002)
