include_guard(GLOBAL)

add_library(awtrix_script_extensions STATIC
  "${AWTRIX_SOURCE_ROOT}/src/platform/linux/script/ExtensionHost.cpp"
  "${AWTRIX_SOURCE_ROOT}/src/platform/linux/script/KnobScripting.cpp")
target_link_libraries(awtrix_script_extensions PUBLIC awtrix_core PRIVATE awtrix_warnings)
