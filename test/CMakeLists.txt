cmake_minimum_required(VERSION 3.20)

project(awtrix_native_tests LANGUAGES C CXX)

include(CTest)
find_package(Threads REQUIRED)

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/ProjectOptions.cmake")
set(CMAKE_CXX_EXTENSIONS ON)

get_filename_component(AWTRIX_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(AWTRIX_DEPS "${AWTRIX_ROOT}/.pio/libdeps/native")
set(UNITY_ROOT "${AWTRIX_DEPS}/Unity")

if(NOT EXISTS "${UNITY_ROOT}/src/unity.c" OR NOT EXISTS "${AWTRIX_DEPS}/base64/src/base64.hpp")
  message(FATAL_ERROR
    "Native dependencies are missing. Run `pio pkg install -e native` first.")
endif()

file(READ "${AWTRIX_ROOT}/version" AWTRIX_VERSION)
string(STRIP "${AWTRIX_VERSION}" AWTRIX_VERSION)

include("${AWTRIX_ROOT}/cmake/Warnings.cmake")
include("${AWTRIX_ROOT}/cmake/Core.cmake")
include("${AWTRIX_ROOT}/cmake/ScriptExtensions.cmake")
target_sources(awtrix_core PRIVATE
  "${AWTRIX_ROOT}/src/transport/ScriptMqttBridge.cpp"
)
target_compile_options(awtrix_core PRIVATE -O2)
target_compile_definitions(awtrix_core PUBLIC PIO_UNIT_TESTING UNIT_TEST)

include("${AWTRIX_ROOT}/cmake/Unity.cmake")

file(GLOB TEST_DIRS LIST_DIRECTORIES true CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/test_*"
)
foreach(TEST_DIR IN LISTS TEST_DIRS)
  if(NOT IS_DIRECTORY "${TEST_DIR}")
    continue()
  endif()

  get_filename_component(TEST_NAME "${TEST_DIR}" NAME)
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${TEST_DIR}/*.cpp")
  if(NOT TEST_SOURCES)
    continue()
  endif()

  add_executable("${TEST_NAME}" ${TEST_SOURCES})
  if(TEST_NAME STREQUAL "test_nativeimage")
    target_sources("${TEST_NAME}" PRIVATE "${TEST_DIR}/jpeg_decoder.c")
    target_include_directories("${TEST_NAME}" PRIVATE "${AWTRIX_ROOT}/lib/TJpg_Decoder/src")
  endif()
  target_compile_options("${TEST_NAME}" PRIVATE -O2)
  target_compile_definitions("${TEST_NAME}" PRIVATE PIO_UNIT_TESTING UNIT_TEST)
  target_link_libraries("${TEST_NAME}" PRIVATE awtrix_core unity)
  if(TEST_NAME STREQUAL "test_scripthost" OR TEST_NAME STREQUAL "test_scriptevents")
    target_link_libraries("${TEST_NAME}" PRIVATE awtrix_script_extensions)
  endif()

  if(WIN32)
    target_link_libraries("${TEST_NAME}" PRIVATE ws2_32)
    target_link_options("${TEST_NAME}" PRIVATE -static-libstdc++ -static-libgcc)
  elseif(UNIX)
    target_link_libraries("${TEST_NAME}" PRIVATE m)
  endif()

  add_test(NAME "${TEST_NAME}" COMMAND "${TEST_NAME}")
  set_tests_properties("${TEST_NAME}" PROPERTIES
    WORKING_DIRECTORY "${AWTRIX_ROOT}"
  )
endforeach()

add_executable(test_font_compact
  test_font/test_font.cpp
  "${AWTRIX_ROOT}/src/core/render/Canvas.cpp"
  "${AWTRIX_ROOT}/src/core/render/TextEncoding.cpp"
  "${AWTRIX_ROOT}/src/core/render/TextRenderer.cpp"
  "${AWTRIX_ROOT}/src/core/render/Color.cpp"
  "${AWTRIX_ROOT}/src/core/render/ColorRamp.cpp"
  "${AWTRIX_ROOT}/src/core/render/Palette.cpp"
)
target_include_directories(test_font_compact PRIVATE "${AWTRIX_ROOT}/src")
target_compile_options(test_font_compact PRIVATE -O2)
target_compile_definitions(test_font_compact PRIVATE AWTRIX_NATIVE PIO_UNIT_TESTING UNIT_TEST AWTRIX_COMPACT_FONTS=1)
target_link_libraries(test_font_compact PRIVATE unity awtrix_warnings)
if(WIN32)
  target_link_options(test_font_compact PRIVATE -static-libstdc++ -static-libgcc)
elseif(UNIX)
  target_link_libraries(test_font_compact PRIVATE m)
endif()
add_test(NAME test_font_compact COMMAND test_font_compact)
set_tests_properties(test_font_compact PROPERTIES WORKING_DIRECTORY "${AWTRIX_ROOT}")

add_executable(test_settings_esp32
  platform_settings/esp32.cpp
  "${AWTRIX_ROOT}/src/core/render/ColorGrade.cpp"
  "${AWTRIX_ROOT}/src/persistence/DeviceConfigJson.cpp"
  "${AWTRIX_ROOT}/src/persistence/SystemConfigApply.cpp"
  "${AWTRIX_ROOT}/src/core/mqtt/HaDiscovery.cpp"
  "${AWTRIX_ROOT}/src/core/Settings.cpp"
  "${AWTRIX_ROOT}/src/core/JsonColor.cpp"
  "${AWTRIX_ROOT}/src/core/WeekdayBarConfig.cpp"
  "${AWTRIX_ROOT}/src/core/payload/ScrollSpec.cpp"
  "${AWTRIX_ROOT}/src/core/render/Color.cpp"
  "${AWTRIX_ROOT}/src/core/apps/BuiltinAppConfig.cpp"
)
target_include_directories(test_settings_esp32 PRIVATE
  "${AWTRIX_ROOT}/src/platform/esp32/include" "${AWTRIX_ROOT}/src")
target_link_libraries(test_settings_esp32 PRIVATE awtrix_json unity awtrix_warnings)
if(WIN32)
  target_link_options(test_settings_esp32 PRIVATE -static-libstdc++ -static-libgcc)
endif()
add_test(NAME test_settings_esp32 COMMAND test_settings_esp32)

add_test(NAME test_font_compact_tables
  COMMAND "${CMAKE_COMMAND}" "-DNM=${CMAKE_NM}" "-DBINARY=$<TARGET_FILE:test_font_compact>"
          -P "${CMAKE_CURRENT_LIST_DIR}/check_compact_fonts.cmake")
