include_guard(GLOBAL)

set(UNITY_ROOT "${AWTRIX_DEPS}/Unity")
add_library(unity STATIC "${UNITY_ROOT}/src/unity.c")
target_include_directories(unity PUBLIC "${UNITY_ROOT}/src")
