include_guard(GLOBAL)

# File, descriptor, clock and SHA-256 helpers for Linux programs; no OpenSSL.
add_library(awtrix_posix STATIC "${CMAKE_CURRENT_LIST_DIR}/../src/platform/posix/Files.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/platform/posix/Resolver.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/platform/posix/sha256.c")
target_include_directories(awtrix_posix PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src")
target_link_libraries(awtrix_posix PRIVATE awtrix_warnings)
find_package(Threads REQUIRED)
target_link_libraries(awtrix_posix PUBLIC Threads::Threads)
