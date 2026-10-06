include_guard(GLOBAL)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(AWTRIX_SANITIZE "Build the host regression tests with ASan and UBSan" ON)
option(AWTRIX_WERROR "Treat compiler warnings in AWTRIX code as errors" ON)
