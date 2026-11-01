include_guard(GLOBAL)
add_library(tc002_voice_net STATIC "${TC002_DIR}/voice/WebSocket.cpp")
target_include_directories(tc002_voice_net PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tc002_voice_net PUBLIC awtrix_host PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads
  awtrix_warnings)
add_library(tc002_voice_session STATIC "${TC002_DIR}/voice/VoiceConfig.cpp" "${TC002_DIR}/voice/AssistSession.cpp")
target_link_libraries(tc002_voice_session PUBLIC tc002_contract awtrix_posix PRIVATE awtrix_core awtrix_warnings)
add_library(tc002_voice STATIC "${TC002_DIR}/voice/VoiceRuntime.cpp" "${TC002_DIR}/voice/VoiceOverlay.cpp"
  "${TC002_DIR}/voice/VoiceMqtt.cpp")
target_link_libraries(tc002_voice PUBLIC tc002_voice_session tc002_voice_net awtrix_host PRIVATE awtrix_warnings)
add_library(tc002_wake_listener STATIC "${TC002_DIR}/wake/WakeListener.cpp")
target_link_libraries(tc002_wake_listener PUBLIC tc002_contract awtrix_posix
  PRIVATE tc002_wake tc002_wake_files awtrix_host awtrix_warnings)
add_library(tc002_microphone_hub STATIC "${TC002_RUNTIME_DIR}/MicrophoneHub.cpp")
target_link_libraries(tc002_microphone_hub PUBLIC tc002_contract PRIVATE awtrix_warnings)
add_library(tc002_voice_router STATIC "${TC002_DIR}/voice/AssistantRouter.cpp")
target_link_libraries(tc002_voice_router PUBLIC tc002_microphone_hub
  PRIVATE tc002_wake_listener awtrix_host awtrix_warnings)
add_library(tc002_voice_clips STATIC "${TC002_DIR}/voice/WavWriter.cpp" "${TC002_DIR}/voice/ClipStore.cpp")
target_include_directories(tc002_voice_clips PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(tc002_voice_clips PUBLIC awtrix_posix PRIVATE awtrix_warnings)
add_library(tc002_voice_capture STATIC "${TC002_DIR}/voice/VoiceCapture.cpp")
target_link_libraries(tc002_voice_capture PUBLIC tc002_voice_clips tc002_wake tc002_microphone_hub tc002_contract
  PRIVATE tc002_wake_files awtrix_warnings)
add_library(tc002_voice_script STATIC "${TC002_DIR}/voice/VoiceScripting.cpp"
  "${TC002_DIR}/voice/VoiceTransfers.cpp")
target_link_libraries(tc002_voice_script PUBLIC tc002_voice_capture awtrix_script_extensions awtrix_host
  PRIVATE tc002_audio awtrix_posix awtrix_warnings)
