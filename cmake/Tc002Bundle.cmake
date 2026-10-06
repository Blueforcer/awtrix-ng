include_guard(GLOBAL)

# Installs the AWTRIX programs and assets for the tc002 bundle component.
# The tc002-loader preset adds libawtrix-loader.so; build_bundle.sh adds third-party content.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(TC002_WEBUI "${PROJECT_BINARY_DIR}/index.html.gz")
add_custom_command(OUTPUT "${TC002_WEBUI}"
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/tc002/install/bundle.py" webui
          "${PROJECT_SOURCE_DIR}" "${TC002_WEBUI}"
  DEPENDS "${PROJECT_SOURCE_DIR}/webui/index.html" "${PROJECT_SOURCE_DIR}/src/transport/http/WebUiAsset.h"
          "${PROJECT_SOURCE_DIR}/tools/tc002/install/bundle.py"
  VERBATIM)
add_custom_target(tc002-webui ALL DEPENDS "${TC002_WEBUI}")

install(TARGETS awtrix-linux awtrix-tc002d awtrix-tc002-dhcp-callback awtrix-tc002-audio-pcm awtrix-tc002-flash
  RUNTIME DESTINATION bin COMPONENT tc002 EXCLUDE_FROM_ALL)
install(FILES "${TC002_WEBUI}" "${PROJECT_SOURCE_DIR}/assets/tc002/boot.mp3" DESTINATION share
  COMPONENT tc002 EXCLUDE_FROM_ALL)
set(TC002_SPEECH_VOICE "${PROJECT_SOURCE_DIR}/assets/speech/voice.atts")
if(EXISTS "${TC002_SPEECH_VOICE}")
  install(FILES "${TC002_SPEECH_VOICE}" DESTINATION share/speech COMPONENT tc002 EXCLUDE_FROM_ALL)
endif()
