include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/Tc002Flash.cmake")

# The Ulanzi TC002: the supervisor contract and update format shared by the programs, the supervisor
# awtrix-tc002d with its DHCP callback, the speaker and flash helpers, and the source lists the
# runtime uses.
set(TC002_DIR "${PROJECT_SOURCE_DIR}/src/platform/tc002")
set(TC002D_DIR "${TC002_DIR}/daemon")
set_source_files_properties(
  "${PROJECT_SOURCE_DIR}/src/core/synth/Player.cpp"
  "${PROJECT_SOURCE_DIR}/src/core/audio/PitchDetector.cpp"
  "${TC002_DIR}/audio/Tc002AudioMixer.cpp"
  PROPERTIES COMPILE_OPTIONS -O2)
# Speech: the English text frontend and the worker that runs a voice for the mixer.
set(TC002_SPEECH_DIR "${TC002_DIR}/speech")
set(TC002_SPEECH_SOURCES "${TC002_SPEECH_DIR}/SpeechText.cpp" "${TC002_SPEECH_DIR}/SpeechPronunciation.cpp"
  "${TC002_SPEECH_DIR}/SpeechLexicon.cpp" "${TC002_SPEECH_DIR}/SpeechRequest.cpp"
  "${TC002_SPEECH_DIR}/SpeechSource.cpp")
add_library(tc002_speech STATIC ${TC002_SPEECH_SOURCES})
target_link_libraries(tc002_speech PUBLIC awtrix_core PRIVATE awtrix_warnings)
# The ATTS model and utterance renderer: -O2, without fused multiply-adds.
set(TC002_SPEECH_VOICE_SOURCES "${TC002_SPEECH_DIR}/SpeechModel.cpp" "${TC002_SPEECH_DIR}/SpeechKernels.cpp"
  "${TC002_SPEECH_DIR}/SpeechWave.cpp" "${TC002_SPEECH_DIR}/SpeechTokens.cpp" "${TC002_SPEECH_DIR}/SpeechSynth.cpp"
  "${TC002_SPEECH_DIR}/SpeechModelVoice.cpp")
set(TC002_SPEECH_VOICE_OPTIONS -O2 -ffp-contract=off)
add_library(tc002_speech_voice STATIC ${TC002_SPEECH_VOICE_SOURCES})
target_compile_options(tc002_speech_voice PRIVATE ${TC002_SPEECH_VOICE_OPTIONS})
target_link_libraries(tc002_speech_voice PUBLIC awtrix_posix PRIVATE awtrix_warnings)

add_library(tc002_contract STATIC "${TC002_DIR}/contract/SupervisorProtocol.cpp"
  "${TC002_DIR}/contract/Pcm16.cpp" "${TC002_DIR}/contract/MicrophoneStream.cpp" "${TC002_DIR}/contract/InputIdentity.cpp")
target_link_libraries(tc002_contract PUBLIC awtrix_json PRIVATE awtrix_warnings)

add_library(tc002_update STATIC "${TC002_DIR}/update/PackageFormat.cpp" "${TC002_DIR}/update/ReleaseManifest.cpp"
  ${TC002_RELEASE_SLOT_SOURCE})
target_link_libraries(tc002_update PUBLIC awtrix_json awtrix_posix PRIVATE awtrix_warnings)
add_library(awtrix_update_state STATIC "${TC002_DIR}/update/UpdateState.cpp"
  "${TC002_DIR}/update/StateDocument.cpp" "${TC002_DIR}/update/FileStateStore.cpp")
target_link_libraries(awtrix_update_state PUBLIC tc002_update PRIVATE awtrix_warnings)
add_library(tc002_update_verify STATIC "${TC002_DIR}/update/PackageVerifier.cpp")
target_link_libraries(tc002_update_verify PUBLIC tc002_update OpenSSL::Crypto PRIVATE awtrix_warnings)
add_executable(awtrix-update-verify "${TC002_DIR}/update/verify_main.cpp")
target_link_libraries(awtrix-update-verify PRIVATE tc002_update_verify awtrix_warnings)
install(TARGETS awtrix-update-verify RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

set(TC002_RUNTIME_DIR "${TC002_DIR}/runtime")
set(TC002_SUPERVISION_SOURCES "${TC002_RUNTIME_DIR}/SupervisorLink.cpp"
  "${TC002_RUNTIME_DIR}/SupervisedRuntime.cpp")
set(TC002_RUNTIME_SOURCES ${TC002_SUPERVISION_SOURCES}
  "${TC002_RUNTIME_DIR}/Tc002BootIntro.cpp" "${TC002_RUNTIME_DIR}/Tc002Board.cpp"
  "${TC002_RUNTIME_DIR}/Tc002ClockApp.cpp" "${TC002_RUNTIME_DIR}/Tc002Input.cpp"
  "${TC002_RUNTIME_DIR}/Tc002QuickSettings.cpp" "${TC002_RUNTIME_DIR}/Tc002StatusApp.cpp"
  "${TC002_RUNTIME_DIR}/Tc002Provisioning.cpp"
  "${TC002_RUNTIME_DIR}/Tc002Target.cpp" "${TC002_RUNTIME_DIR}/Tc002Update.cpp")
set(TC002_BOOT_SCREEN_SOURCES "${TC002_RUNTIME_DIR}/BootIntroWide.cpp" "${TC002_RUNTIME_DIR}/BootInfoWide.cpp"
  "${TC002_RUNTIME_DIR}/TerminalFont.cpp")
# The runtime's speaker: the sink with its mixer, sources and player, and speech in front of it.
add_library(tc002_audio STATIC
  "${TC002_DIR}/audio/Tc002AudioLink.cpp"
  "${TC002_DIR}/audio/Tc002AudioSources.cpp"
  "${TC002_DIR}/audio/Tc002AudioStream.cpp"
  "${TC002_DIR}/audio/Tc002AudioPlayer.cpp"
  "${TC002_DIR}/audio/Tc002AudioMixer.cpp"
  "${TC002_DIR}/audio/UrlSounds.cpp"
  "${TC002_DIR}/audio/Tc002AudioSink.cpp"
  "${TC002_DIR}/audio/Tc002AudioRequests.cpp"
  "${TC002_DIR}/audio/Tc002AudioSongs.cpp"
  "${TC002_DIR}/audio/Tc002AudioPlayback.cpp"
  "${TC002_DIR}/audio/Tc002AudioPlan.cpp")
target_link_libraries(tc002_audio PUBLIC tc002_speech awtrix_host PRIVATE awtrix_warnings)
# The speaker helper awtrix-tc002-audio-pcm, the daemon's client of the awtrix_pcm driver.
set(TC002_AUDIO_PCM_CORE "${TC002_DIR}/audio/helper/audio_helper.c" "${TC002_DIR}/audio/pcm/pcm_backend.c")
set(TC002_AUDIO_PCM_FRAME "${TC002_DIR}/audio/helper/audio_owner.c" "${TC002_DIR}/audio/helper/audio_process.c")
add_executable(awtrix-tc002-audio-pcm "${TC002_DIR}/audio/pcm/pcm_helper_main.c" ${TC002_AUDIO_PCM_CORE}
  ${TC002_AUDIO_PCM_FRAME})
set_target_properties(awtrix-tc002-audio-pcm PROPERTIES C_EXTENSIONS OFF)
target_link_libraries(awtrix-tc002-audio-pcm PRIVATE awtrix_warnings)

# The install and flash helper tc002_install.py pushes to the device.
set(TC002_FLASH_VERSION "${AWTRIX_VERSION}")
if(AWTRIX_RELEASE_VERSION)
  set(TC002_FLASH_VERSION "${AWTRIX_RELEASE_VERSION}")
endif()
add_executable(awtrix-tc002-flash "${TC002_DIR}/flasher/main.c"
  "${TC002_DIR}/flasher/mtd_device.c" ${TC002_FLASH_C_SOURCES})
set_target_properties(awtrix-tc002-flash PROPERTIES C_EXTENSIONS OFF)
target_compile_definitions(awtrix-tc002-flash PRIVATE _GNU_SOURCE
  "AWTRIX_FLASH_VERSION=\"${TC002_FLASH_VERSION}\"")
target_link_libraries(awtrix-tc002-flash PRIVATE awtrix_warnings)

add_library(tc002d_base STATIC "${TC002D_DIR}/Log.cpp" "${TC002D_DIR}/Process.cpp" "${TC002D_DIR}/Kernel.cpp"
  "${TC002D_DIR}/PropertyWorkspace.cpp")
target_link_libraries(tc002d_base PUBLIC awtrix_posix PRIVATE awtrix_warnings)

set(TC002_WIFI_CORE_SOURCES
  "${TC002D_DIR}/wifi/WifiCrypto.cpp"
  "${TC002D_DIR}/wifi/CredentialStore.cpp"
  "${TC002D_DIR}/wifi/SupplicantConfig.cpp"
  "${TC002D_DIR}/wifi/WpaControl.cpp"
  "${TC002D_DIR}/wifi/WifiSystem.cpp")
set(TC002D_WIFI_SOURCES ${TC002_WIFI_CORE_SOURCES} "${TC002D_DIR}/wifi/WifiService.cpp"
  "${TC002D_DIR}/wifi/WifiScan.cpp" "${TC002D_DIR}/wifi/SupplicantSession.cpp")
set(TC002D_IP_SOURCES
  "${TC002D_DIR}/ip/CaptiveDns.cpp"
  "${TC002D_DIR}/ip/ChildProcess.cpp"
  "${TC002D_DIR}/ip/ClientOutput.cpp"
  "${TC002D_DIR}/ip/DhcpLease.cpp"
  "${TC002D_DIR}/ip/IpController.cpp"
  "${TC002D_DIR}/ip/IpService.cpp"
  "${TC002D_DIR}/ip/LeaseApplier.cpp"
  "${TC002D_DIR}/ip/Mdns.cpp"
  "${TC002D_DIR}/ip/Netlink.cpp"
  "${TC002D_DIR}/ip/ResolvConf.cpp"
  "${TC002D_DIR}/ip/Sntp.cpp"
  "${TC002D_DIR}/ip/SystemPlatform.cpp")

add_library(tc002_mcu_firmware STATIC "${TC002D_DIR}/McuFirmware.cpp")
target_link_libraries(tc002_mcu_firmware PUBLIC awtrix_json awtrix_posix PRIVATE awtrix_warnings)

add_library(tc002d_core STATIC
  "${TC002D_DIR}/Autostart.cpp"
  "${TC002D_DIR}/BtService.cpp"
  "${TC002D_DIR}/ControlSocket.cpp"
  "${TC002D_DIR}/DeviceId.cpp"
  "${TC002D_DIR}/EventLoop.cpp"
  "${TC002D_DIR}/HardwareLease.cpp"
  "${TC002D_DIR}/McuFrame.cpp"
  "${TC002D_DIR}/McuPcm.cpp"
  "${TC002D_DIR}/McuStream.cpp"
  "${TC002D_DIR}/McuService.cpp"
  "${TC002D_DIR}/McuUpgrade.cpp"
  "${TC002D_DIR}/Options.cpp"
  "${TC002D_DIR}/RuntimeChild.cpp"
  "${TC002D_DIR}/MicrophoneRelay.cpp"
  "${TC002D_DIR}/RuntimeLog.cpp"
  "${TC002D_DIR}/ForwardedLog.cpp"
  "${TC002D_DIR}/SpeakerBackend.cpp"
  "${TC002D_DIR}/StartReason.cpp"
  "${TC002D_DIR}/Status.cpp"
  "${TC002D_DIR}/StockApp.cpp"
  "${TC002D_DIR}/update/Package.cpp"
  "${TC002D_DIR}/update/ReleaseSlot.cpp"
  "${TC002D_DIR}/update/UpdateRecord.cpp"
  "${TC002D_DIR}/update/UpdateService.cpp"
  ${TC002_MTD_TABLE_SOURCE})
target_compile_definitions(tc002d_core PUBLIC "AWTRIX_NG_VERSION=\"${AWTRIX_VERSION}\"")
target_link_libraries(tc002d_core PUBLIC tc002d_base tc002_contract tc002_update tc002_mcu_firmware awtrix_update_state
  PRIVATE awtrix_warnings)

add_executable(awtrix-tc002d "${TC002D_DIR}/main.cpp" ${TC002D_WIFI_SOURCES} ${TC002D_IP_SOURCES})
target_link_libraries(awtrix-tc002d PRIVATE tc002d_core awtrix_warnings)

if(AWTRIX_TC002_BUNDLE OR NOT CMAKE_CROSSCOMPILING)
find_package(LibLZMA REQUIRED)
add_library(tc002_mcu_prepare STATIC "${TC002_DIR}/mcu/McuPrepare.cpp" "${TC002_DIR}/mcu/McuPatch.cpp")
target_include_directories(tc002_mcu_prepare PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(tc002_mcu_prepare PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(tc002_mcu_prepare PRIVATE tc002_mcu_firmware LibLZMA::LibLZMA OpenSSL::SSL OpenSSL::Crypto Threads::Threads awtrix_warnings)
# Standalone host/bench test wrapper; production reuses awtrix-linux's TLS runtime.
add_executable(awtrix-tc002-mcu-prepare "${TC002_DIR}/mcu/prepare_cli.cpp")
target_link_libraries(awtrix-tc002-mcu-prepare PRIVATE tc002_mcu_prepare)
endif()
add_executable(awtrix-tc002-dhcp-callback "${TC002D_DIR}/ip/DhcpCallbackMain.cpp")
set_target_properties(awtrix-tc002-dhcp-callback PROPERTIES OUTPUT_NAME dhcp-callback)
target_link_libraries(awtrix-tc002-dhcp-callback PRIVATE awtrix_posix awtrix_warnings)
