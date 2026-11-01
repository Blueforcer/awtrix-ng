include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/Warnings.cmake")

if(AWTRIX_SANITIZE AND UNIX AND NOT CMAKE_CROSSCOMPILING AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
  target_compile_options(awtrix_warnings INTERFACE -Wno-error)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(--param=asan-globals=0)
  endif()
endif()
