include_guard(GLOBAL)

# The host services the Linux runtime shares with the native tests: stores, HTTP, MQTT, TLS
# and media, with the vendored PubSubClient and TJpgDec.
add_library(pubsubclient STATIC "${PROJECT_SOURCE_DIR}/lib/PubSubClient/src/PubSubClient.cpp")
target_include_directories(pubsubclient SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/lib/PubSubClient/src")
target_include_directories(pubsubclient PUBLIC "${PROJECT_SOURCE_DIR}/src/platform/linux/host/compat"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/include" "${PROJECT_SOURCE_DIR}/src")
add_library(tjpgd STATIC "${PROJECT_SOURCE_DIR}/lib/TJpg_Decoder/src/tjpgd.c")
target_include_directories(tjpgd PUBLIC "${PROJECT_SOURCE_DIR}/lib/TJpg_Decoder/src")

file(GLOB HOST_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/platform/linux/host/*.cpp")
list(REMOVE_ITEM HOST_SOURCES "${PROJECT_SOURCE_DIR}/src/platform/linux/host/HostScriptHeap.cpp")
add_library(awtrix_host STATIC ${HOST_SOURCES})
target_sources(awtrix_host PRIVATE
  "${PROJECT_SOURCE_DIR}/src/platform/linux/mqtt/Socket.cpp"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/mqtt/Commands.cpp"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/tls/TlsPolicy.cpp"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/tls/TlsTrust.cpp"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/tls/ServerIdentity.cpp"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/tls/BrokerTrust.cpp"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/tls/BrokerTrustApi.cpp"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/net/FileDownload.cpp"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/net/HttpClient.cpp"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/net/HttpGet.cpp"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/LinuxMemory.cpp"
  "${PROJECT_SOURCE_DIR}/src/media/AssetFileDevice.cpp"
  "${PROJECT_SOURCE_DIR}/src/media/IconRenderer.cpp"
  "${PROJECT_SOURCE_DIR}/src/media/JpegDecoder.cpp"
  "${PROJECT_SOURCE_DIR}/src/media/GifPlayer.cpp"
  "${PROJECT_SOURCE_DIR}/src/media/MicroGif.cpp"
  "${PROJECT_SOURCE_DIR}/src/media/ScriptIcon.cpp"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/images/RemoteScriptIcon.cpp"
  "${PROJECT_SOURCE_DIR}/src/persistence/DeviceConfigJson.cpp"
  "${PROJECT_SOURCE_DIR}/src/persistence/SystemConfigApply.cpp"
  "${PROJECT_SOURCE_DIR}/src/persistence/SystemConfigApi.cpp"
  "${PROJECT_SOURCE_DIR}/src/persistence/FsRestoreSink.cpp"
  "${PROJECT_SOURCE_DIR}/src/system/Log.cpp"
  "${PROJECT_SOURCE_DIR}/src/transport/net/UdpSocket.cpp"
  "${PROJECT_SOURCE_DIR}/src/transport/ScriptMqttBridge.cpp"
  "${PROJECT_SOURCE_DIR}/src/transport/mqtt/MqttService.cpp"
  "${PROJECT_SOURCE_DIR}/src/transport/mqtt/MqttLink.cpp"
  "${PROJECT_SOURCE_DIR}/src/transport/mqtt/HaAnnouncer.cpp")
target_include_directories(awtrix_host PUBLIC "${PROJECT_SOURCE_DIR}/src/platform/linux/host/compat")
target_compile_definitions(awtrix_host PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(awtrix_host PUBLIC awtrix_core pubsubclient OpenSSL::SSL OpenSSL::Crypto
  PRIVATE tjpgd awtrix_posix awtrix_warnings)
