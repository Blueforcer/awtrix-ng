include_guard(GLOBAL)

# SHA-256, a worker thread and small helpers any host builds; the file and descriptor helpers of
# the Linux programs on top. No OpenSSL.
find_package(Threads REQUIRED)
add_library(awtrix_portable STATIC "${CMAKE_CURRENT_LIST_DIR}/../src/platform/posix/Digest.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/platform/posix/WorkerThread.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../src/platform/posix/sha256.c")
target_include_directories(awtrix_portable PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src")
target_link_libraries(awtrix_portable PUBLIC Threads::Threads PRIVATE awtrix_warnings)

if(UNIX)
  add_library(awtrix_posix STATIC "${CMAKE_CURRENT_LIST_DIR}/../src/platform/posix/Files.cpp")
  target_link_libraries(awtrix_posix PUBLIC awtrix_portable PRIVATE awtrix_warnings)
endif()
