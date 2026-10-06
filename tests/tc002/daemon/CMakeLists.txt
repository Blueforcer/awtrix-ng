add_executable(tc002d-fake-runtime fake_runtime.cpp)
add_executable(tc002d-fake-audio fake_audio.cpp)
foreach(fixture tc002d-fake-runtime tc002d-fake-audio)
  target_link_libraries(${fixture} PRIVATE awtrix_warnings)
endforeach()

set(fakes "$<TARGET_FILE:tc002d-fake-runtime>" "$<TARGET_FILE:tc002d-fake-audio>")
foreach(contract autostart bt mcu mcupcm mcustream mcufirmware lease loop stock kernel process start)
  add_awtrix_test(NAME tc002d-${contract} SOURCES test_${contract}.cpp LIBRARIES tc002d_core Threads::Threads)
endforeach()
add_awtrix_test(NAME tc002d-control SOURCES test_control.cpp LIBRARIES tc002d_core Threads::Threads
  COMMAND "$<TARGET_FILE:awtrix-tc002d>")
add_awtrix_test(NAME tc002d-speaker SOURCES test_speaker.cpp LIBRARIES tc002d_core Threads::Threads
  COMMAND ${fakes} TIMEOUT 90)
add_awtrix_test(NAME tc002d-runtime SOURCES test_runtime.cpp "${PROJECT_SOURCE_DIR}/tests/ManualClock.cpp"
  LIBRARIES tc002d_core Threads::Threads
  COMMAND ${fakes} TIMEOUT 90)
target_link_options(tc002d-runtime-test PRIVATE "-Wl,--wrap=clock_gettime")
add_awtrix_test(NAME tc002d-update SOURCES test_update.cpp LIBRARIES tc002d_core Threads::Threads
  COMMAND "$<TARGET_FILE:awtrix-tc002d>" "$<TARGET_FILE:tc002d-fake-runtime>" "${Python3_EXECUTABLE}"
          "${PROJECT_SOURCE_DIR}/tools/update/package.py"
  TIMEOUT 90)

add_subdirectory(ip)
add_subdirectory(wifi)
