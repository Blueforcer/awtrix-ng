include_guard(GLOBAL)

add_library(awtrix_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(awtrix_warnings INTERFACE -Wall -Wextra $<$<BOOL:${AWTRIX_WERROR}>:-Werror>)
endif()
