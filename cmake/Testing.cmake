include_guard(GLOBAL)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(AWTRIX_WEBUI "${PROJECT_SOURCE_DIR}/webui/index.html")

# add_awtrix_test(NAME name
#                 [SOURCES file...] [LIBRARIES library...] [INCLUDES directory...] [DEFINITIONS definition...]
#                 [SANITIZE] [COMMAND argument...] [TIMEOUT seconds] [LABELS label...] [PROPERTIES name value...])
#
# With SOURCES the test builds the executable name-test under the warning policy (and ASan and
# UBSan with SANITIZE while AWTRIX_SANITIZE is on) and runs it with the COMMAND arguments; without
# SOURCES, COMMAND is the whole command line. TIMEOUT defaults to 30 seconds.
function(add_awtrix_test)
  cmake_parse_arguments(PARSE_ARGV 0 TEST "SANITIZE" "NAME;TIMEOUT"
                        "SOURCES;LIBRARIES;INCLUDES;DEFINITIONS;COMMAND;LABELS;PROPERTIES")
  if(NOT TEST_NAME)
    message(FATAL_ERROR "add_awtrix_test needs a NAME")
  endif()
  if(TEST_SOURCES)
    set(target "${TEST_NAME}-test")
    add_executable(${target} ${TEST_SOURCES})
    target_link_libraries(${target} PRIVATE ${TEST_LIBRARIES} awtrix_warnings)
    if(TEST_INCLUDES)
      target_include_directories(${target} PRIVATE ${TEST_INCLUDES})
    endif()
    if(TEST_DEFINITIONS)
      target_compile_definitions(${target} PRIVATE ${TEST_DEFINITIONS})
    endif()
    if(TEST_SANITIZE)
      awtrix_sanitize(${target})
    endif()
    add_test(NAME ${TEST_NAME} COMMAND ${target} ${TEST_COMMAND})
  else()
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_COMMAND})
  endif()
  if(NOT TEST_TIMEOUT)
    set(TEST_TIMEOUT 30)
  endif()
  set_tests_properties(${TEST_NAME} PROPERTIES TIMEOUT ${TEST_TIMEOUT} ${TEST_PROPERTIES})
  if(TEST_LABELS)
    set_tests_properties(${TEST_NAME} PROPERTIES LABELS "${TEST_LABELS}")
  endif()
endfunction()

# add_awtrix_runtime_test(NAME name SCRIPT file [ARGUMENTS argument...] [TIMEOUT seconds] [LABELS label...]
#                         [PROPERTIES name value...])
#
# A Python contract that drives the awtrix-linux binary with the web UI.
function(add_awtrix_runtime_test)
  cmake_parse_arguments(PARSE_ARGV 0 TEST "" "NAME;SCRIPT;TIMEOUT" "ARGUMENTS;LABELS;PROPERTIES")
  get_filename_component(script "${TEST_SCRIPT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  set(forwarded)
  foreach(keyword TIMEOUT LABELS PROPERTIES)
    if(TEST_${keyword})
      list(APPEND forwarded ${keyword} ${TEST_${keyword}})
    endif()
  endforeach()
  add_awtrix_test(NAME ${TEST_NAME}
    COMMAND "${Python3_EXECUTABLE}" "${script}" --binary "$<TARGET_FILE:awtrix-linux>"
            --webui "${AWTRIX_WEBUI}" ${TEST_ARGUMENTS}
    ${forwarded})
endfunction()

function(awtrix_sanitize target)
  if(AWTRIX_SANITIZE AND UNIX AND NOT CMAKE_CROSSCOMPILING AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()
