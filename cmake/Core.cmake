include_guard(GLOBAL)

# The portable engine the ESP32 firmware builds too: Berry, the JSON reader and awtrix_core. Needs
# AWTRIX_DEPS (base64), AWTRIX_VERSION, Threads and cmake/Warnings.cmake.
get_filename_component(AWTRIX_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

file(GLOB_RECURSE BERRY_SOURCES CONFIGURE_DEPENDS "${AWTRIX_SOURCE_ROOT}/lib/berry/src/*.c"
  "${AWTRIX_SOURCE_ROOT}/lib/berry/default/*.c")
add_library(berry STATIC ${BERRY_SOURCES})
target_include_directories(berry SYSTEM PUBLIC "${AWTRIX_SOURCE_ROOT}/lib/berry/src" "${AWTRIX_SOURCE_ROOT}/lib/berry")

add_library(awtrix_json STATIC "${AWTRIX_SOURCE_ROOT}/src/core/api/JsonReader.cpp")
target_include_directories(awtrix_json PUBLIC "${AWTRIX_SOURCE_ROOT}/src")
target_link_libraries(awtrix_json PRIVATE awtrix_warnings)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "${AWTRIX_SOURCE_ROOT}/src/core/*.c"
  "${AWTRIX_SOURCE_ROOT}/src/core/*.cpp")
list(REMOVE_ITEM CORE_SOURCES "${AWTRIX_SOURCE_ROOT}/src/core/api/JsonReader.cpp")
add_library(awtrix_script_heap STATIC "${AWTRIX_SOURCE_ROOT}/src/platform/linux/host/HostScriptHeap.cpp")
target_include_directories(awtrix_script_heap PRIVATE "${AWTRIX_SOURCE_ROOT}/src")
target_link_libraries(awtrix_script_heap PRIVATE awtrix_warnings)

add_library(awtrix_core STATIC ${CORE_SOURCES}
  "${AWTRIX_SOURCE_ROOT}/src/platform/linux/settings/Settings.cpp"
  "${AWTRIX_SOURCE_ROOT}/src/platform/linux/settings/Config.cpp"
  "${AWTRIX_SOURCE_ROOT}/src/platform/linux/render/OutputGrade.cpp"
  "${AWTRIX_SOURCE_ROOT}/src/platform/linux/render/PageZoom.cpp")
target_include_directories(awtrix_core PUBLIC "${AWTRIX_SOURCE_ROOT}/src/platform/linux/include")
target_include_directories(awtrix_core PUBLIC "${AWTRIX_SOURCE_ROOT}/src")
target_include_directories(awtrix_core SYSTEM PUBLIC "${AWTRIX_DEPS}/base64/src")
target_compile_definitions(awtrix_core PUBLIC AWTRIX_NATIVE "AWTRIX_NG_VERSION=\"${AWTRIX_VERSION}\"")
target_link_libraries(awtrix_core PUBLIC awtrix_json berry awtrix_script_heap Threads::Threads PRIVATE awtrix_warnings)
if(UNIX)
  target_link_libraries(awtrix_core PUBLIC m)
endif()
