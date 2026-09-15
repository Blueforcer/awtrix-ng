cmake_minimum_required(VERSION 3.20)

project(awtrix_native_tests LANGUAGES C CXX)

include(CTest)
find_package(Threads REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

get_filename_component(AWTRIX_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(NATIVE_LIBDEPS "${AWTRIX_ROOT}/.pio/libdeps/native")
set(UNITY_ROOT "${NATIVE_LIBDEPS}/Unity")
set(BASE64_ROOT "${NATIVE_LIBDEPS}/base64")

if(NOT EXISTS "${UNITY_ROOT}/src/unity.c" OR NOT EXISTS "${BASE64_ROOT}/src/base64.hpp")
  message(FATAL_ERROR
    "Native dependencies are missing. Run `pio pkg install -e native` first.")
endif()

file(READ "${AWTRIX_ROOT}/version" AWTRIX_VERSION)
string(STRIP "${AWTRIX_VERSION}" AWTRIX_VERSION)

file(GLOB_RECURSE BERRY_SOURCES CONFIGURE_DEPENDS
  "${AWTRIX_ROOT}/lib/berry/src/*.c"
  "${AWTRIX_ROOT}/lib/berry/default/*.c"
)
add_library(berry STATIC ${BERRY_SOURCES})
target_include_directories(berry PUBLIC
  "${AWTRIX_ROOT}/lib/berry/src"
  "${AWTRIX_ROOT}/lib/berry"
  "${AWTRIX_ROOT}/lib/berry/default"
)

file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
  "${AWTRIX_ROOT}/src/core/*.c"
  "${AWTRIX_ROOT}/src/core/*.cpp"
)
add_library(awtrix_core STATIC
  ${CORE_SOURCES}
  "${AWTRIX_ROOT}/src/transport/ScriptMqttBridge.cpp"
)
target_compile_options(awtrix_core PRIVATE -O2)
target_compile_definitions(awtrix_core PUBLIC
  AWTRIX_NATIVE
  PIO_UNIT_TESTING
  UNIT_TEST
  "AWTRIX_NG_VERSION=\"${AWTRIX_VERSION}\""
)
target_include_directories(awtrix_core PUBLIC
  "${AWTRIX_ROOT}/src"
  "${BASE64_ROOT}/src"
)
target_link_libraries(awtrix_core PUBLIC berry Threads::Threads)

add_library(unity STATIC "${UNITY_ROOT}/src/unity.c")
target_include_directories(unity PUBLIC "${UNITY_ROOT}/src")

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
  target_compile_options("${TEST_NAME}" PRIVATE -O2)
  target_compile_definitions("${TEST_NAME}" PRIVATE PIO_UNIT_TESTING UNIT_TEST)
  target_link_libraries("${TEST_NAME}" PRIVATE awtrix_core unity)

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
