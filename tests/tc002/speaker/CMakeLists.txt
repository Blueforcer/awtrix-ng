set(audio "${PROJECT_SOURCE_DIR}/src/platform/tc002/audio")
set(sources INCLUDES "${PROJECT_SOURCE_DIR}/src")

add_awtrix_test(NAME tc002-audio-owner SOURCES test_audio_owner.c ${sources})
add_awtrix_test(NAME tc002-audio-pcm-helper SOURCES test_pcm_helper.c ${TC002_AUDIO_PCM_CORE}
  ${TC002_AUDIO_PCM_FRAME} ${sources} TIMEOUT 60)

add_executable(tc002-audio-check-host "${audio}/helper/audio_check.c")
add_executable(tc002-audio-fake-pcm-helper fake_pcm_helper_main.c ${TC002_AUDIO_PCM_CORE} ${TC002_AUDIO_PCM_FRAME})
target_include_directories(tc002-audio-fake-pcm-helper PRIVATE "${PROJECT_SOURCE_DIR}/src")
foreach(fixture tc002-audio-check-host tc002-audio-fake-pcm-helper)
  target_link_libraries(${fixture} PRIVATE awtrix_warnings)
endforeach()

set(check "$<TARGET_FILE:tc002-audio-check-host>")
add_awtrix_test(NAME tc002-audio-pcm-check-tone COMMAND ${check} "$<TARGET_FILE:tc002-audio-fake-pcm-helper>"
  30 440 400 TIMEOUT 20)
add_awtrix_test(NAME tc002-audio-pcm-check-stop COMMAND ${check} "$<TARGET_FILE:tc002-audio-fake-pcm-helper>"
  30 440 3000 300 TIMEOUT 20)
add_awtrix_test(NAME tc002-audio-pcm-check-preflight COMMAND ${check} "$<TARGET_FILE:awtrix-tc002-audio-pcm>"
  10 440 100 TIMEOUT 20 PROPERTIES PASS_REGULAR_EXPRESSION "helper exit=3")
