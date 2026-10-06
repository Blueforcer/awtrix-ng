include_guard(GLOBAL)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB_RECURSE WEBUI_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/webui/src/*")
set(WEBUI_HTML "${PROJECT_SOURCE_DIR}/webui/index.html")
add_custom_command(OUTPUT "${WEBUI_HTML}"
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/webui_source.py"
  DEPENDS ${WEBUI_SOURCES}
    "${PROJECT_SOURCE_DIR}/scripts/webui_source.py" "${PROJECT_SOURCE_DIR}/scripts/berry_api.py"
    "${PROJECT_SOURCE_DIR}/src/core/script/ScriptBindings.cpp"
    "${PROJECT_SOURCE_DIR}/src/core/script/Prelude.h"
    "${PROJECT_SOURCE_DIR}/src/platform/linux/layout/LayoutModule.h"
    "${PROJECT_SOURCE_DIR}/src/platform/tc002/audio/AudioScriptModule.h"
    "${PROJECT_SOURCE_DIR}/lib/berry/src/be_baselib.c"
    "${PROJECT_SOURCE_DIR}/lib/berry/berry_conf.h"
  VERBATIM)
add_custom_target(awtrix-webui ALL DEPENDS "${WEBUI_HTML}")
