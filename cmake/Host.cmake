include_guard(GLOBAL)

# The host services awtrix-linux and the script host share with the tests: stores, HTTP, MQTT, TLS
# and media, with the vendored PubSubClient and TJpgDec; the TCP, sign-in, secret and picture
# libraries of scripts on top.
include("${CMAKE_CURRENT_LIST_DIR}/ScriptExtensions.cmake")
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
  "${PROJECT_SOURCE_DIR}/src/platform/linux/net/HttpClient.cpp"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/net/HttpGet.cpp"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/net/Multipart.cpp"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/net/PrivateHost.cpp"
  "${PROJECT_SOURCE_DIR}/src/platform/linux/net/TransferQueue.cpp"
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
  "${PROJECT_SOURCE_DIR}/src/transport/net/UdpPort.cpp"
  "${PROJECT_SOURCE_DIR}/src/transport/net/HostLookup.cpp"
  "${PROJECT_SOURCE_DIR}/src/transport/ScriptMqttBridge.cpp"
  "${PROJECT_SOURCE_DIR}/src/transport/mqtt/MqttService.cpp"
  "${PROJECT_SOURCE_DIR}/src/transport/mqtt/MqttLink.cpp"
  "${PROJECT_SOURCE_DIR}/src/transport/mqtt/HaAnnouncer.cpp")
target_include_directories(awtrix_host PUBLIC "${PROJECT_SOURCE_DIR}/src/platform/linux/host/compat")
target_compile_definitions(awtrix_host PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)
target_link_libraries(awtrix_host PUBLIC awtrix_core awtrix_os pubsubclient OpenSSL::SSL OpenSSL::Crypto
  PRIVATE tjpgd awtrix_warnings)
if(TARGET awtrix_posix)
  target_link_libraries(awtrix_host PRIVATE awtrix_posix)
endif()

set(HOST_LIB_DIR "${PROJECT_SOURCE_DIR}/src/platform/linux")
add_library(awtrix_tcp STATIC "${HOST_LIB_DIR}/script/TcpClients.cpp" "${HOST_LIB_DIR}/script/TcpScripting.cpp")
target_link_libraries(awtrix_tcp PUBLIC awtrix_script_extensions awtrix_os Threads::Threads PRIVATE awtrix_warnings)

# Sign-in for scripts: the @oauth line, its private records, token handling and HTTP routes.
add_library(awtrix_oauth STATIC "${HOST_LIB_DIR}/oauth/OAuthSpec.cpp" "${HOST_LIB_DIR}/oauth/OAuthFlow.cpp"
  "${HOST_LIB_DIR}/oauth/OAuthVault.cpp" "${HOST_LIB_DIR}/oauth/OAuthService.cpp"
  "${HOST_LIB_DIR}/oauth/OAuthTransport.cpp" "${HOST_LIB_DIR}/oauth/OAuthApi.cpp"
  "${HOST_LIB_DIR}/oauth/OAuthScripting.cpp")
target_link_libraries(awtrix_oauth PUBLIC awtrix_script_extensions awtrix_os awtrix_host Threads::Threads
  PRIVATE OpenSSL::Crypto awtrix_warnings)

add_library(awtrix_secrets STATIC "${HOST_LIB_DIR}/secrets/SecretSpec.cpp" "${HOST_LIB_DIR}/secrets/SecretService.cpp"
  "${HOST_LIB_DIR}/secrets/SecretApi.cpp" "${HOST_LIB_DIR}/secrets/SecretScripting.cpp")
target_link_libraries(awtrix_secrets PUBLIC awtrix_script_extensions awtrix_os awtrix_host Threads::Threads
  PRIVATE awtrix_warnings)

# Pictures from URLs: fetched, fitted and kept for every icon, script and layout that names one.
find_package(JPEG REQUIRED)
add_library(awtrix_images STATIC "${HOST_LIB_DIR}/images/HttpPictureLoader.cpp"
  "${HOST_LIB_DIR}/images/PictureDecoder.cpp" "${HOST_LIB_DIR}/images/Inflate.cpp"
  "${HOST_LIB_DIR}/images/JpegPicture.cpp" "${HOST_LIB_DIR}/images/PictureFit.cpp"
  "${HOST_LIB_DIR}/images/PngPicture.cpp" "${HOST_LIB_DIR}/images/RemoteImageStore.cpp")
target_link_libraries(awtrix_images PUBLIC awtrix_host PRIVATE JPEG::JPEG awtrix_warnings)
