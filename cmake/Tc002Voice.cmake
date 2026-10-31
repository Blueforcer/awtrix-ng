include_guard(GLOBAL)
add_library(tc002_voice_net STATIC "${TC002_DIR}/voice/WebSocket.cpp")
target_include_directories(tc002_voice_net PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tc002_voice_net PUBLIC awtrix_host PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads
  awtrix_warnings)
add_library(tc002_voice_session STATIC "${TC002_DIR}/voice/VoiceConfig.cpp" "${TC002_DIR}/voice/AssistSession.cpp")
target_link_libraries(tc002_voice_session PUBLIC tc002_contract awtrix_posix PRIVATE awtrix_warnings)
add_library(tc002_voice STATIC "${TC002_DIR}/voice/VoiceRuntime.cpp" "${TC002_DIR}/voice/VoiceOverlay.cpp"
  "${TC002_DIR}/voice/VoiceMqtt.cpp")
target_link_libraries(tc002_voice PUBLIC tc002_voice_session tc002_voice_net awtrix_host PRIVATE awtrix_warnings)
# The wake word: the listener on the microphone stream that starts Voice, around the tc002_wake engine.
add_library(tc002_wake_listener STATIC "${TC002_DIR}/wake/WakeListener.cpp")
target_link_libraries(tc002_wake_listener PUBLIC tc002_wake tc002_wake_files tc002_contract awtrix_host
  Threads::Threads PRIVATE awtrix_warnings)
# Shares the daemon's one microphone stream among several consumers.
add_library(tc002_microphone_hub STATIC "${TC002_RUNTIME_DIR}/MicrophoneHub.cpp")
target_link_libraries(tc002_microphone_hub PUBLIC tc002_contract PRIVATE awtrix_warnings)
