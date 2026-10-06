# The TC002 loader: the Arm GNU Toolchain 9.2-2019.12 for arm-none-linux-gnueabihf whose prefix
# TC002_GLIBC_PREFIX names, as a cache variable or in the environment
# (tools/tc002/toolchain/env.sh). Its glibc 2.30 is the stock userspace's.
set(TC002_PREFIX_VARIABLE TC002_GLIBC_PREFIX)
include("${CMAKE_CURRENT_LIST_DIR}/tc002-common.cmake")
# Shared objects install with mode 0755, as the release manifest lists them.
set(CMAKE_INSTALL_SO_NO_EXE 0)
