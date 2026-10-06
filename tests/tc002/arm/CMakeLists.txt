# The contracts that must also hold on the device's CPU, built by the tc002 preset and run under the
# toolchain's emulator (qemu-arm): the speaker helper's protocol and owner scan, the refusals of the
# helper as it ships, the flash helper's write and verify logic, and the voice with its NEON dot
# product against the model's reference runtime.
if(NOT CMAKE_CROSSCOMPILING_EMULATOR)
  message(STATUS "No qemu-arm: the ARM contracts are not registered")
  return()
endif()

set(speaker "${PROJECT_SOURCE_DIR}/tests/tc002/speaker")
add_awtrix_test(NAME tc002-arm-audio-pcm-helper SOURCES "${speaker}/test_pcm_helper.c" ${TC002_AUDIO_PCM_CORE}
  ${TC002_AUDIO_PCM_FRAME} INCLUDES "${PROJECT_SOURCE_DIR}/src" TIMEOUT 120)
add_awtrix_test(NAME tc002-arm-audio-owner SOURCES "${speaker}/test_audio_owner.c"
  INCLUDES "${PROJECT_SOURCE_DIR}/src")
add_awtrix_test(NAME tc002-arm-audio-pcm-refusals
  COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/check_helper_refusals.sh" "$<TARGET_FILE:awtrix-tc002-audio-pcm>"
          ${CMAKE_CROSSCOMPILING_EMULATOR})

add_awtrix_test(NAME tc002-arm-flasher SOURCES "${PROJECT_SOURCE_DIR}/tests/tc002/flasher/test_flasher.c"
  ${TC002_FLASH_C_SOURCES}
  INCLUDES "${PROJECT_SOURCE_DIR}/src" TIMEOUT 60)

set(runtime "${PROJECT_SOURCE_DIR}/tests/tc002/runtime")
add_awtrix_test(NAME tc002-arm-speech-voice SOURCES "${runtime}/test_speech_voice.cpp" ${TC002_SPEECH_VOICE_SOURCES}
  LIBRARIES awtrix_posix COMMAND "${runtime}/speech" TIMEOUT 300)
target_compile_options(tc002-arm-speech-voice-test PRIVATE ${TC002_SPEECH_VOICE_OPTIONS})
