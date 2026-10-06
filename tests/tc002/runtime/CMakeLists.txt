set(linux "${PROJECT_SOURCE_DIR}/src/platform/linux")
add_awtrix_test(NAME tc002-clock SOURCES test_tc002_clock.cpp "${TC002_RUNTIME_DIR}/Tc002ClockApp.cpp"
  LIBRARIES awtrix_core unity SANITIZE)
add_awtrix_test(NAME tc002-status SOURCES test_tc002_status.cpp "${TC002_RUNTIME_DIR}/Tc002StatusApp.cpp"
  LIBRARIES awtrix_core unity SANITIZE)
add_awtrix_test(NAME tc002-audio-script SOURCES test_audio_script.cpp LIBRARIES tc002_audio_script SANITIZE)
add_awtrix_test(NAME tc002-speech SOURCES test_speech.cpp ${TC002_SPEECH_SOURCES} LIBRARIES awtrix_core SANITIZE)
add_awtrix_test(NAME tc002-speech-source SOURCES test_speech_source.cpp ${TC002_SPEECH_SOURCES}
  "${TC002_DIR}/audio/Tc002AudioMixer.cpp" "${TC002_DIR}/audio/Tc002AudioSources.cpp" LIBRARIES awtrix_core SANITIZE
  TIMEOUT 60)
add_test(NAME tc002-speech-lexicon COMMAND tc002-speech-test --lexicon
  "${PROJECT_SOURCE_DIR}/tools/speech/esdb35-us-application.tsv")
add_awtrix_test(NAME tc002-speech-voice SOURCES test_speech_voice.cpp ${TC002_SPEECH_VOICE_SOURCES}
  LIBRARIES awtrix_posix SANITIZE COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/speech" TIMEOUT 120)
target_compile_options(tc002-speech-voice-test PRIVATE ${TC002_SPEECH_VOICE_OPTIONS})
add_awtrix_test(NAME tc002-voice-overlay SOURCES test_voice_overlay.cpp
  "${TC002_DIR}/voice/VoiceOverlay.cpp" LIBRARIES awtrix_core)
add_executable(tc002-voice-websocket-probe test_voice_websocket.cpp)
target_link_libraries(tc002-voice-websocket-probe PRIVATE tc002_voice_net awtrix_warnings)
add_awtrix_test(NAME tc002-voice-websocket COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/test_voice_websocket.py" "$<TARGET_FILE:tc002-voice-websocket-probe>" TIMEOUT 60)
add_awtrix_test(NAME tc002-assist-session SOURCES test_assist_session.cpp LIBRARIES tc002_voice_session)
add_awtrix_test(NAME tc002-url-sounds SOURCES test_url_sounds.cpp "${TC002_DIR}/audio/UrlSounds.cpp"
  LIBRARIES awtrix_host awtrix_posix TIMEOUT 30)
add_awtrix_test(NAME tc002-voice-gesture SOURCES test_voice_gesture.cpp
  INCLUDES "${PROJECT_SOURCE_DIR}/src")
add_awtrix_test(NAME tc002-voice-mqtt SOURCES test_voice_mqtt.cpp "${TC002_DIR}/voice/VoiceMqtt.cpp"
  LIBRARIES awtrix_core)
add_awtrix_test(NAME tc002-microphone-input SOURCES test_microphone_input.cpp LIBRARIES awtrix_core tc002_contract)
add_awtrix_test(NAME tc002-music-pitch SOURCES test_music_pitch.cpp LIBRARIES awtrix_core tc002_contract)

add_awtrix_test(NAME tc002-supervisor-link SOURCES test_supervisor_link.cpp ${TC002_SUPERVISION_SOURCES}
  LIBRARIES awtrix_host tc002_contract TIMEOUT 10)
add_awtrix_test(NAME tc002-board SOURCES test_tc002_board.cpp "${TC002_RUNTIME_DIR}/Tc002Board.cpp" LIBRARIES awtrix_core)
add_awtrix_test(NAME tc002-input SOURCES test_tc002_input.cpp "${TC002_RUNTIME_DIR}/Tc002Input.cpp"
  INCLUDES "${PROJECT_SOURCE_DIR}/src" LIBRARIES tc002_contract)
add_awtrix_test(NAME tc002-quick-settings SOURCES test_tc002_quick_settings.cpp "${TC002_RUNTIME_DIR}/Tc002QuickSettings.cpp"
  LIBRARIES awtrix_core)
add_awtrix_test(NAME tc002-update SOURCES test_tc002_update.cpp "${TC002_RUNTIME_DIR}/Tc002Update.cpp"
  LIBRARIES awtrix_host tc002_contract tc002_update_verify awtrix_update_state)
add_awtrix_test(NAME tc002-runtime-speaker SOURCES test_speaker.cpp LIBRARIES tc002_audio)
add_awtrix_test(NAME tc002-audio-sink SOURCES test_audio_sink.cpp
  INCLUDES "${PROJECT_SOURCE_DIR}/test" LIBRARIES tc002_audio TIMEOUT 90)
add_awtrix_test(NAME tc002-audio-end-to-end SOURCES test_audio_end_to_end.cpp ${TC002_AUDIO_PCM_CORE}
  INCLUDES "${PROJECT_SOURCE_DIR}/test" "${TC002_TEST_ROOT}" LIBRARIES tc002_audio TIMEOUT 90)

add_awtrix_test(NAME tc002-provisioning SOURCES test_provisioning.cpp
  "${TC002_RUNTIME_DIR}/Tc002Provisioning.cpp" LIBRARIES awtrix_host)
